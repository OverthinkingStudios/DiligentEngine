#include "buildings.h"
#include "EarthworksDebug.h"
#include "vegetationBuilder.h"   // shaderLightBuffer

#include <spdlog/spdlog.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <vector>

// PORT NOTE: logic restored from the commented-out "rappersville" blocks in
// terrain.cpp (~897 load, ~3926 LightsCB wiring, ~5266 render) and
// Earthworks_4.cpp (~288 atmosphere textures). See buildings.h.

void buildingsRenderer::load(const std::string& _basePath)
{
    spdlog::info("buildings: loading {}", _basePath);

    int numVerts = 0;
    {
        std::ifstream ifs(_basePath + ".info.txt");
        if (!ifs)
        {
            spdlog::warn("buildings: missing {}.info.txt - buildings disabled", _basePath);
            return;
        }
        ifs >> numVerts;
    }
    if (numVerts < 3)
    {
        spdlog::warn("buildings: {}.info.txt reports {} verts - buildings disabled", _basePath, numVerts);
        return;
    }

    std::vector<vertex> verts(numVerts);
    {
        std::ifstream ifs(_basePath + ".raw", std::ios::binary);
        if (!ifs)
        {
            spdlog::warn("buildings: missing {}.raw - buildings disabled", _basePath);
            return;
        }
        ifs.read(reinterpret_cast<char*>(verts.data()), verts.size() * sizeof(vertex));
        if (ifs.gcount() != static_cast<std::streamsize>(verts.size() * sizeof(vertex)))
        {
            spdlog::warn("buildings: {}.raw shorter than {} verts x 48 B - buildings disabled", _basePath, numVerts);
            return;
        }
    }

    buildChunks(verts);

    vertexData = Buffer::createStructured(sizeof(vertex), numVerts);
    vertexData->setBlob(verts.data(), 0, numVerts * sizeof(vertex));
    cpuVerts = std::move(verts);        // kept for overlayShadowHeights()

    shader.load("Samples/Earthworks_4/hlsl/terrain/render_Buildings_Far.hlsl", "vsMain", "psMain", Vao::Topology::TriangleList);
    shader.Vars()->setBuffer("vertexBuffer", vertexData);

    // The original never bound samplers here (Falcor supplied defaults); the
    // compat layer's dummy-sampler fallback would probably cover it, but this
    // is new code, so bind them explicitly. shadow() uses gSmpLinear,
    // sunLight()/atmosphere use gSmpLinearClamp - linear clamp suits both.
    {
        Sampler::Desc samplerDesc;
        samplerDesc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp)
                   .setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
        sampler_Clamp = Sampler::create(samplerDesc);
        shader.Vars()->setSampler("gSmpLinear", sampler_Clamp);
        shader.Vars()->setSampler("gSmpLinearClamp", sampler_Clamp);
    }

    numTriangles = numVerts / 3;
    spdlog::info("buildings: {} verts, {} triangles, {} grid chunks", numVerts, numTriangles, chunks.size());
}


// Bucket triangles into a fixed x/z grid (by centroid) and reorder _verts so
// every cell is one contiguous range; fills `chunks`. This is what lets
// render() skip whole cells that overlap no visible terrain tile.
void buildingsRenderer::buildChunks(std::vector<vertex>& _verts)
{
    const size_t numTris = _verts.size() / 3;

    float2 dataMin(FLT_MAX, FLT_MAX);
    float2 dataMax(-FLT_MAX, -FLT_MAX);
    for (const vertex& v : _verts)
    {
        dataMin.x = std::min(dataMin.x, v.pos.x);
        dataMin.y = std::min(dataMin.y, v.pos.z);
        dataMax.x = std::max(dataMax.x, v.pos.x);
        dataMax.y = std::max(dataMax.y, v.pos.z);
    }

    // ~256 m cells match the mid-lod terrain tiles; cap the grid at 64x64 so
    // a huge data set just gets bigger cells instead of thousands of chunks.
    float cellSize = 256.f;
    while ((dataMax.x - dataMin.x) / cellSize > 64.f || (dataMax.y - dataMin.y) / cellSize > 64.f)
        cellSize *= 2.f;
    const int cellsX = std::max(1, (int)std::ceil((dataMax.x - dataMin.x) / cellSize));
    const int cellsY = std::max(1, (int)std::ceil((dataMax.y - dataMin.y) / cellSize));

    auto cellOf = [&](const vertex* tri) -> int
    {
        float cx = (tri[0].pos.x + tri[1].pos.x + tri[2].pos.x) / 3.f;
        float cz = (tri[0].pos.z + tri[1].pos.z + tri[2].pos.z) / 3.f;
        int x = std::clamp((int)((cx - dataMin.x) / cellSize), 0, cellsX - 1);
        int y = std::clamp((int)((cz - dataMin.y) / cellSize), 0, cellsY - 1);
        return y * cellsX + x;
    };

    // counting sort of triangles by cell, stable and O(n)
    std::vector<uint32_t> cellCount(cellsX * cellsY, 0);
    for (size_t t = 0; t < numTris; t++)
        cellCount[cellOf(&_verts[t * 3])]++;

    std::vector<uint32_t> cellStart(cellsX * cellsY);   // in triangles
    uint32_t running = 0;
    for (size_t c = 0; c < cellCount.size(); c++)
    {
        cellStart[c] = running;
        running += cellCount[c];
    }

    std::vector<vertex> sorted(_verts.size());
    std::vector<uint32_t> cellFill = cellStart;
    for (size_t t = 0; t < numTris; t++)
    {
        uint32_t dst = cellFill[cellOf(&_verts[t * 3])]++;
        for (int i = 0; i < 3; i++)
            sorted[dst * 3 + i] = _verts[t * 3 + i];
    }
    _verts.swap(sorted);

    chunks.clear();
    for (size_t c = 0; c < cellCount.size(); c++)
    {
        if (cellCount[c] == 0) continue;

        chunk ch;
        ch.firstVertex = cellStart[c] * 3;
        ch.numVertices = cellCount[c] * 3;
        ch.bbMin = float2(FLT_MAX, FLT_MAX);
        ch.bbMax = float2(-FLT_MAX, -FLT_MAX);
        for (uint32_t v = ch.firstVertex; v < ch.firstVertex + ch.numVertices; v++)
        {
            ch.bbMin.x = std::min(ch.bbMin.x, _verts[v].pos.x);
            ch.bbMin.y = std::min(ch.bbMin.y, _verts[v].pos.z);
            ch.bbMax.x = std::max(ch.bbMax.x, _verts[v].pos.x);
            ch.bbMax.y = std::max(ch.bbMax.y, _verts[v].pos.z);
        }
        chunks.push_back(ch);
    }
}


void buildingsRenderer::overlayShadowHeights(float* _height, int _dim, float _metersPerPixel) const
{
    if (cpuVerts.empty()) return;

    // Continuous grid coords: world origin maps to the grid centre, cell i's
    // centre sits at i + 0.5 - the texel-centre convention shadow() samples
    // with (uv = pos.xz / (dim * mpp) + 0.5).
    const float half = _dim * 0.5f;
    auto splat = [&](int _x, int _y, float _h)
    {
        if (_x < 0 || _y < 0 || _x >= _dim || _y >= _dim) return;
        float& cell = _height[_y * _dim + _x];
        cell = std::max(cell, _h);
    };

    const size_t numTris = cpuVerts.size() / 3;
    for (size_t t = 0; t < numTris; t++)
    {
        const vertex* tri = &cpuVerts[t * 3];
        float2 g[3];
        for (int i = 0; i < 3; i++)
        {
            g[i] = float2(tri[i].pos.x / _metersPerPixel + half,
                          tri[i].pos.z / _metersPerPixel + half);
            // Vertices always splat their own cell, so buildings smaller than
            // one cell (~10 m) still register.
            splat((int)std::floor(g[i].x), (int)std::floor(g[i].y), tri[i].pos.y);
        }

        // Walls are vertical (zero x/z footprint) - their top edge is covered
        // by the vertex splats above; only roof triangles have area to fill.
        const float det = (g[1].x - g[0].x) * (g[2].y - g[0].y) - (g[2].x - g[0].x) * (g[1].y - g[0].y);
        if (std::abs(det) < 1e-6f) continue;

        const int minX = std::max(0, (int)std::floor(std::min({ g[0].x, g[1].x, g[2].x })));
        const int maxX = std::min(_dim - 1, (int)std::ceil(std::max({ g[0].x, g[1].x, g[2].x })));
        const int minY = std::max(0, (int)std::floor(std::min({ g[0].y, g[1].y, g[2].y })));
        const int maxY = std::min(_dim - 1, (int)std::ceil(std::max({ g[0].y, g[1].y, g[2].y })));

        for (int y = minY; y <= maxY; y++)
        {
            for (int x = minX; x <= maxX; x++)
            {
                const float2 p(x + 0.5f, y + 0.5f);
                const float b1 = ((p.x - g[0].x) * (g[2].y - g[0].y) - (g[2].x - g[0].x) * (p.y - g[0].y)) / det;
                const float b2 = ((g[1].x - g[0].x) * (p.y - g[0].y) - (p.x - g[0].x) * (g[1].y - g[0].y)) / det;
                const float b0 = 1.f - b1 - b2;
                if (b0 < 0.f || b1 < 0.f || b2 < 0.f) continue;
                splat(x, y, b0 * tri[0].pos.y + b1 * tri[1].pos.y + b2 * tri[2].pos.y);
            }
        }
    }

    spdlog::info("buildings: overlaid {} triangles onto the {}x{} shadow height grid", numTris, _dim, _dim);
}


void buildingsRenderer::updateShaderConstants(Texture::SharedPtr _terrainShadow, const shaderLightBuffer& _buffer)
{
    if (!loaded()) return;

    shader.Vars()->setTexture("terrainShadow", _terrainShadow);

    shader.Vars()["LightsCB"]["sunDirection"] = _buffer.sunDirection;
    shader.Vars()["LightsCB"]["sunRightVector"] = _buffer.sunRightVector;
    shader.Vars()["LightsCB"]["sunUpVector"] = _buffer.sunUpVector;
    shader.Vars()["LightsCB"]["screenSize"] = _buffer.screenSize;
    shader.Vars()["LightsCB"]["fog_far_Start"] = _buffer.fog_far_Start;
    shader.Vars()["LightsCB"]["fog_far_log_F"] = _buffer.fog_far_log_F;
    shader.Vars()["LightsCB"]["fog_far_one_over_k"] = _buffer.fog_far_one_over_k;
}


void buildingsRenderer::setAtmosphere(Texture::SharedPtr _inscatter, Texture::SharedPtr _outscatter, Texture::SharedPtr _sunlight)
{
    if (!loaded()) return;

    shader.Vars()->setTexture("gAtmosphereInscatter", _inscatter);
    shader.Vars()->setTexture("gAtmosphereOutscatter", _outscatter);
    shader.Vars()->setTexture("SunInAtmosphere", _sunlight);
}


void buildingsRenderer::render(RenderContext* _renderContext, const Fbo::SharedPtr& _fbo, const GraphicsState::Viewport& _viewport,
                               const rmcv::mat4& _view, const rmcv::mat4& _viewproj, const float3& _eye,
                               const std::vector<float4>& _visibleTileRects)
{
    if (!loaded() || !ew::gDebug.toggles.buildings) return;

    
    shader.State()->setFbo(_fbo);
    shader.State()->setViewport(0, _viewport, true);
    shader.Vars()["PerFrameCB"]["view"] = _view;
    shader.Vars()["PerFrameCB"]["viewproj"] = _viewproj;
    shader.Vars()["PerFrameCB"]["eye"] = _eye;

    auto chunkVisible = [&](const chunk& _c) -> bool
    {
        if (_visibleTileRects.empty()) return true;     // no visibility info - draw everything
        for (const float4& r : _visibleTileRects)
        {
            if (_c.bbMin.x <= r.x + r.z && _c.bbMax.x >= r.x &&
                _c.bbMin.y <= r.y + r.z && _c.bbMax.y >= r.y)
                return true;
        }
        return false;
    };

    // Walk the chunks (contiguous vertex ranges) and merge visible neighbours
    // into as few draws as possible. The range start goes through the CB
    // (firstVertex) - SV_VertexID does NOT include StartVertexLocation on
    // D3D, so a plain startVertex offset silently drew from vertex 0 and
    // showed the wrong (far-corner) buildings.
    uint32_t runStart = 0, runCount = 0;
    auto flush = [&]()
    {
        if (runCount == 0) return;
        shader.Vars()["PerFrameCB"]["firstVertex"] = runStart;
        shader.drawInstanced(_renderContext, runCount, 1);
        ew::gDebug.live.buildingDraws++;
        runCount = 0;
    };

    for (const chunk& c : chunks)
    {
        if (!chunkVisible(c))
        {
            flush();
            continue;
        }
        if (runCount == 0) runStart = c.firstVertex;
        runCount = c.firstVertex + c.numVertices - runStart;
    }
    flush();
}
