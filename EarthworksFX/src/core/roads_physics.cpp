
#include"roads_physics.h"
#include "imgui.h"
#include "imgui_internal.h"



extern void cubic_Casteljau_Full(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3, glm::vec3& pos, glm::vec3& vel, glm::vec3& acc);
extern glm::vec3 cubic_Casteljau(float t, glm::vec3 P0, glm::vec3 P1, glm::vec3 P2, glm::vec3 P3);
extern glm::vec3 cubic_Casteljau(float t, bezierPoint* A, bezierPoint* B);
extern glm::vec3 del_cubic_Casteljau(float t0, float t1, bezierPoint* A, bezierPoint* B);


/*  bezierLayer
    FIXME this may well flip stuf but mte to double bezier first then fix
    --------------------------------------------------------------------------------------------------------------------*/
bezierLayer::bezierLayer(bezier_edge inner, bezier_edge outer, uint _material, uint bezierIndex, bool _left, float w0, float w1, bool startSeg, bool endSeg)
{
    if (bezierIndex == 0) {
        bool bCM = true;
    }
    A = 0;
    B = 0;

    A = (inner << 31) + (outer << 30);
    // leaves 2 bits free for now

    // max 4095 materials
    A += (_material & 0x7ff) << 17;
    A += bezierIndex & 0x1ffff;

    //-16 to plus 16m in 2mm jumps
    w0 += 16.0f;
    w1 += 16.0f;
    w0 = __min(32.0f, __max(0.0f, w0));
    w1 = __min(32.0f, __max(0.0f, w1));
    B = (((int)(w0 * 500.0f)) << 14) + ((int)(w1 * 500.0f));

    if (startSeg)	B += 1 << 31;
    if (endSeg)		B += 1 << 30;
    if (_left)      B += 1 << 28;
}



/*  plane_2d
    --------------------------------------------------------------------------------------------------------------------*/
void plane_2d::frompoints(glm::vec3 A, glm::vec3 B) {

    glm::vec3 perpendicular = B - A;
    norm = glm::normalize(glm::vec2(perpendicular.z, -perpendicular.x));
    dst = glm::dot(glm::vec2(A.x, A.z), norm);
}



float plane_2d::distance(glm::vec2 P) {
    return(glm::dot(P, norm) - dst);
}



/*  physicsBezier
*   FIXME should become vec4 with the distance included
    --------------------------------------------------------------------------------------------------------------------*/
physicsBezier::physicsBezier(splinePoint a, splinePoint b, bool bRight, uint _guid, uint _index, bool _fullWidth)
{
    bRighthand = bRight;
    roadGUID = _guid;
    index = _index;

    layers.clear();

    if (_fullWidth)
    {
        glm::vec3 C = (a.bezier[left].pos + a.bezier[right].pos + b.bezier[left].pos + b.bezier[right].pos) * 0.25f;
        float dA = glm::length(C - a.bezier[left].pos);
        float dB = glm::length(C - a.bezier[right].pos);
        float dC = glm::length(C - b.bezier[left].pos);
        float dD = glm::length(C - b.bezier[right].pos);
        float dst = __max(__max(dA, dB), __max(dC, dD)) * 1.3f;	// max and scale bigger for outer blends  1.3 is thumbsuck

        center = glm::vec2(C.x, C.z);
        radius = dst;
        //		radiusSquare = dst*dst;
        startPlane.frompoints(a.bezier[left].pos, a.bezier[right].pos);
        endPlane.frompoints(b.bezier[left].pos, b.bezier[right].pos);

        // unlike for the GPU we epand it here with dupicates. Cache miss is far more critical than trying to save some memory, unlike on the GPU
        // that has fantastic cache for the vertex pipeline
        bezier[0][0] = glm::vec4(a.bezier[left].pos, 0);
        bezier[0][1] = glm::vec4(a.bezier[left].forward(), 0);
        bezier[0][2] = glm::vec4(b.bezier[left].backward(), 0);
        bezier[0][3] = glm::vec4(b.bezier[left].pos, 0);

        bezier[1][0] = glm::vec4(a.bezier[right].pos, 0);
        bezier[1][1] = glm::vec4(a.bezier[right].forward(), 0);
        bezier[1][2] = glm::vec4(b.bezier[right].backward(), 0);
        bezier[1][3] = glm::vec4(b.bezier[right].pos, 0);

        layers.clear();
    }
    else if (bRight)
    {
        glm::vec3 C = (a.bezier[middle].pos + a.bezier[right].pos + b.bezier[middle].pos + b.bezier[right].pos) * 0.25f;
        float dA = glm::length(C - a.bezier[middle].pos);
        float dB = glm::length(C - a.bezier[right].pos);
        float dC = glm::length(C - b.bezier[middle].pos);
        float dD = glm::length(C - b.bezier[right].pos);
        float dst = __max(__max(dA, dB), __max(dC, dD)) * 1.3f;	// max and scale bigger for outer blends  1.3 is thumbsuck

        center = glm::vec2(C.x, C.z);
        radius = dst;
        //		radiusSquare = dst*dst;
        startPlane.frompoints(a.bezier[middle].pos, a.bezier[right].pos);
        endPlane.frompoints(b.bezier[middle].pos, b.bezier[right].pos);

        // unlike for the GPU we epand it here with dupicates. Cache miss is far more critical than trying to save some memory, unlike on the GPU
        // that has fantastic cache for the vertex pipeline
        bezier[0][0] = glm::vec4(a.bezier[middle].pos, 0);
        bezier[0][1] = glm::vec4(a.bezier[middle].forward(), 0);
        bezier[0][2] = glm::vec4(b.bezier[middle].backward(), 0);
        bezier[0][3] = glm::vec4(b.bezier[middle].pos, 0);

        bezier[1][0] = glm::vec4(a.bezier[right].pos, 0);
        bezier[1][1] = glm::vec4(a.bezier[right].forward(), 0);
        bezier[1][2] = glm::vec4(b.bezier[right].backward(), 0);
        bezier[1][3] = glm::vec4(b.bezier[right].pos, 0);

        layers.clear();
    }
    else
    {
        glm::vec3 C = (a.bezier[middle].pos + a.bezier[left].pos + b.bezier[middle].pos + b.bezier[left].pos) * 0.25f;
        float dA = glm::length(C - a.bezier[middle].pos);
        float dB = glm::length(C - a.bezier[left].pos);
        float dC = glm::length(C - b.bezier[middle].pos);
        float dD = glm::length(C - b.bezier[left].pos);
        float dst = __max(__max(dA, dB), __max(dC, dD)) * 1.3f;	// max and scale bigger for outer blends  1.3 is thumbsuck

        center = glm::vec2(C.x, C.z);
        radius = dst;
        //		radiusSquare = dst*dst;
        startPlane.frompoints(a.bezier[middle].pos, a.bezier[left].pos);
        endPlane.frompoints(b.bezier[middle].pos, b.bezier[left].pos);

        // unlike for the GPU we epand it here with dupicates. Cache miss is far more critical than trying to save some memory
        bezier[0][0] = glm::vec4(a.bezier[middle].pos, 0);
        bezier[0][1] = glm::vec4(a.bezier[middle].backward(), 0);
        bezier[0][2] = glm::vec4(b.bezier[middle].forward(), 0);
        bezier[0][3] = glm::vec4(b.bezier[middle].pos, 0);

        bezier[1][0] = glm::vec4(a.bezier[left].pos, 0);
        bezier[1][1] = glm::vec4(a.bezier[left].backward(), 0);
        bezier[1][2] = glm::vec4(b.bezier[left].forward(), 0);
        bezier[1][3] = glm::vec4(b.bezier[left].pos, 0);

        layers.clear();
    }
}



void physicsBezier::addLayer(bezier_edge inner, bezier_edge outer, uint material, float w0, float w1)
{
    physicsBezierLayer layer;
    layer.material = material;

    // translate w0 and w1 into absolute values
    // FIXME dink nog n keer hieroor, begin eers lookup skryf, kyk dan weer
    //layer.vOffset = 0;
    //layer.vScale = 1;
    layer.edge[0] = inner;
    layer.edge[1] = outer;
    layer.offset[0] = w0;
    layer.offset[1] = w1;

    layers.push_back(layer);
}



void physicsBezier::binary_import(FILE* file)
{
    fread(&center, 1, sizeof(glm::vec2), file);
    fread(&radius, 1, sizeof(float), file);
    fread(&startPlane, 1, sizeof(plane_2d), file);
    fread(&endPlane, 1, sizeof(plane_2d), file);
    fread(&cacheIndex, 1, sizeof(int), file);
    fread(&bezier[0][0], 2 * 4, sizeof(glm::vec4), file);

    uint numL;
    fread(&numL, 1, sizeof(uint), file);
    layers.clear();
    for (uint i = 0; i < numL; i++) {
        physicsBezierLayer layer;
        fread(&layer, 1, sizeof(physicsBezierLayer), file);
        layers.push_back(layer);
    }
}



void physicsBezier::binary_export(FILE* file)
{
    fwrite(&center, 1, sizeof(glm::vec2), file);
    fwrite(&radius, 1, sizeof(float), file);
    fwrite(&startPlane, 1, sizeof(plane_2d), file);
    fwrite(&endPlane, 1, sizeof(plane_2d), file);
    fwrite(&cacheIndex, 1, sizeof(int), file);
    fwrite(&bezier[0][0], 2 * 4, sizeof(glm::vec4), file);

    uint numL = (uint)layers.size();
    fwrite(&numL, 1, sizeof(uint), file);
    for (uint i = 0; i < numL; i++) {
        fwrite(&layers[i], 1, sizeof(physicsBezierLayer), file);
    }
}


std::vector<physicsBezierLayer> layers;




/*  bezierCache
    --------------------------------------------------------------------------------------------------------------------*/
uint bezierCache::findEmpty()
{
    uint maxAge = 0;
    uint slot = 0;
    for (int i = 0; i < cache.size(); i++) {
        if (cache[i].counter > maxAge) {
            slot = i;
            maxAge = cache[i].counter;
        }
    }
    return slot;
}



void bezierCache::solveStats()
{
}







/*	Figure out if its already cached or cache is not
    what the hell does everything point at ??? ;-) */
void bezierCache::cacheSpline(physicsBezier* pBezier, uint cacheSlot)
{
    pBezier->cacheIndex = cacheSlot;

    cache[cacheSlot].counter = 0;

    for (int i = 0; i <= numBezierCache; i++) {

        float t = (float)i / (float)numBezierCache;
        cache[cacheSlot].points[i].A = cubic_Casteljau(t, pBezier->bezier[0][0], pBezier->bezier[0][1], pBezier->bezier[0][2], pBezier->bezier[0][3]);
        cache[cacheSlot].points[i].B = cubic_Casteljau(t, pBezier->bezier[1][0], pBezier->bezier[1][1], pBezier->bezier[1][2], pBezier->bezier[1][3]);
        cache[cacheSlot].points[i].planeTangent.frompoints(cache[cacheSlot].points[i].A, cache[cacheSlot].points[i].B);
    }

    for (int i = 0; i < numBezierCache; i++) {
        cache[cacheSlot].points[i].planeInner.frompoints(cache[cacheSlot].points[i + 1].A, cache[cacheSlot].points[i].A);
        cache[cacheSlot].points[i].planeOuter.frompoints(cache[cacheSlot].points[i + 1].B, cache[cacheSlot].points[i].B);
    }
}



void bezierCache::increment()
{
    for (auto& item : cache) {
        item.counter++;
    }
}



void bezierCache::clear()
{
    for (auto& item : cache) {
        item.bezierIndex = -1;
        item.counter = 10000;
    }
}



/*  bezierOneIntersection
    --------------------------------------------------------------------------------------------------------------------*/
bezierOneIntersection::bezierOneIntersection(glm::vec2 P, uint boundingIndex)
{
    pos = P;
    boundIndex = boundingIndex;
    bHit = false;
    distanceTillNextSearch = 0;
    cacheIndex = -1;
    t_idx = 0;
}



void bezierOneIntersection::updatePosition(glm::vec2 P, float distanceTraveled)
{
    distanceTillNextSearch -= distanceTraveled;
    pos = P;
}



/*  bezierIntersection
    --------------------------------------------------------------------------------------------------------------------*/
void bezierIntersection::updatePosition(glm::vec2 P)
{
    float distanceTraveled = glm::length(pos - P);
    pos = P;
    for (auto& patch : beziers) {
        patch.updatePosition(P, distanceTraveled);
    }
}




/*  bezierFastGrid
    --------------------------------------------------------------------------------------------------------------------*/
void bezierFastGrid::allocate(float sz, uint numX, uint numY)
{
    size = sz;
    nX = numX;
    nY = numY;
    offset = int2(nX >> 1, nY >> 1);
    //lookup.resize(nX * nY);
    //data.reserve(); maybe not, too hard to guess, messes with binary load

    buidData.resize(nX * nY);
    maxHash = (nX * nY) - 1;
}



void bezierFastGrid::populateBezier()
{
}



/*	For performance reasons this does not do bounds checking
    In practive we should always be in bounds in the game, and getBlockLookup does include bounds checks, so a bad value here is
    of no particular consequince*/
inline uint bezierFastGrid::getHash(glm::vec2 pos)
{
    return ((int)floor((pos.y) / size + offset.y) * nX) + (int)floor((pos.x) / size + offset.x);
}



std::vector<uint> bezierFastGrid::getBlockLookup(uint hash)
{
    return buidData[__min(hash, maxHash)];
}



// FIXME this selightly overestimate by doeing AABB instead of sphere but would need to return a list then
void bezierFastGrid::insertBezier(glm::vec2 pos, float radius, uint index)
{
    int2 min, max;
    min.x = __max(0, (int)floor((pos.x - radius) / size + offset.x));
    min.y = __max(0, (int)floor((pos.y - radius) / size + offset.y));

    max.x = __min((int)nX - 1, (int)floor((pos.x + radius) / size + offset.x));
    max.y = __min((int)nY - 1, (int)floor((pos.y + radius) / size + offset.y));

    for (int y = min.y; y <= max.y; y++)
    {
        for (int x = min.x; x <= max.x; x++)
        {
            buidData[y * nX + x].push_back(index);
        }
    }

}



void bezierFastGrid::binary_import(FILE* file)
{
    fread(&size, 1, sizeof(float), file);
    fread(&offset, 1, sizeof(int2), file);
    fread(&nX, 1, sizeof(uint), file);
    fread(&nY, 1, sizeof(uint), file);
    fread(&maxHash, 1, sizeof(uint), file);

    buidData.resize(nX * nY);
    for (uint y = 0; y < (nX * nY); y++) {
        uint numBezier;
        fread(&numBezier, 1, sizeof(uint), file);
        buidData[y].clear();
        buidData[y].resize(numBezier);
        for (uint x = 0; x < numBezier; x++) {
            uint bezierIndex;
            fread(&bezierIndex, 1, sizeof(uint), file);
            buidData[y][x] = bezierIndex;
        }
    }
}



void bezierFastGrid::binary_export(FILE* file)
{
    fwrite(&size, 1, sizeof(float), file);
    fwrite(&offset, 1, sizeof(int2), file);
    fwrite(&nX, 1, sizeof(uint), file);
    fwrite(&nY, 1, sizeof(uint), file);
    fwrite(&maxHash, 1, sizeof(uint), file);

    uint datasize = nX * nY;// (uint)buidData.size();
    for (uint y = 0; y < datasize; y++) {
        uint numBezier = (uint)buidData[y].size();
        fwrite(&numBezier, 1, sizeof(uint), file);
        for (uint x = 0; x < numBezier; x++) {
            fwrite(&buidData[y][x], 1, sizeof(uint), file);
        }
    }
}



/*  ODE_bezier
    --------------------------------------------------------------------------------------------------------------------*/
void ODE_bezier::intersect(bezierIntersection* pI)
{
    cache.increment();

    // First check the cell-hash to see if we crossed boundaries
    // It would be faster if we can see which beziers exists in both and transfer the existing solution over, but for nwo this should be fine
    {
        //		PROFILE(grid);
        if (gridLookup.getHash(pI->pos) != pI->cellHash) {
            pI->cellHash = gridLookup.getHash(pI->pos);
            pI->beziers.clear();

            std::vector<uint> cell = gridLookup.getBlockLookup(pI->cellHash);
            for (uint bezierIndex : cell) {
                pI->beziers.push_back(bezierOneIntersection(pI->pos, bezierIndex));		// I am still not sure this is fast enough, look at array<> again
            }
        }
    }

    {
        //		PROFILE(intersectBezier);
        pI->numHits = 0;
        for (auto& bezier : pI->beziers)
        {
            if (bezier.bHit) {
                //if (needsReCache)
                {
                    // this bloxk re-cache the data
                    physicsBezier* pPhysics = &bezierBounding[bezier.boundIndex];
                    cache.cacheSpline(pPhysics, bezier.cacheIndex);
                }
                pI->numHits += intersectBezier(&bezier);
            }
            else if (bezier.distanceTillNextSearch <= 0) {
                if (testBounds(&bezier)) {
                    pI->numHits += intersectBezier(&bezier);
                }
            }
        }
    }

    needsReCache = false;

    // Now sum and merge
    {
        //		PROFILE(sumandmerge);
        for (auto& bezier : pI->beziers)
        {
            if (bezier.bHit)
            {
                physicsBezier* pPhysics = &bezierBounding[bezier.boundIndex];
                for (auto& layer : pPhysics->layers)
                {
                    float V0 = 0;	// FXIME FO nicely with arrays
                    float V1 = 0;
                    if (layer.edge[0] == bezier_edge::center) {
                        V0 = bezier.d0 - layer.offset[0];
                    }
                    else {
                        V0 = bezier.d1 - layer.offset[0];
                    }

                    if (layer.edge[1] == bezier_edge::center) {
                        V1 = bezier.d0 - layer.offset[1];
                    }
                    else {
                        V1 = bezier.d1 - layer.offset[1];
                    }

                    float V = V0 / (V0 - V1);

                    if (V >= 0 && V <= 1.0f)
                    {
                        // FIXME BLEND WITH ALPHA
                        pI->height = bezier.bezierHeight;

                        switch (layer.material) {
                        case 0:
                            pI->grip = 1.0f;
                            break;
                        case 1:
                            pI->grip = 0.6f;
                            break;
                        case 2:
                            pI->grip = 0.3f;
                            break;
                        }
                    }
                }
            }
        }
    }

}



bool ODE_bezier::intersectBezier(bezierOneIntersection* pI)
{
    if (pI->cacheIndex < 0) return false;

    //	PROFILE(intersectBezier);

    float d0 = 0;
    float d1 = 0;

    cacheItem* C = &cache.cache[pI->cacheIndex];	// fixme more likely an iterator is better
    C->counter = 0;

    d0 = C->points[pI->t_idx].planeTangent.distance(pI->pos);
    while ((pI->t_idx > -1) && (d0 < 0.0f)) {
        pI->t_idx--;
        d0 = C->points[pI->t_idx].planeTangent.distance(pI->pos);
    };

    if (pI->t_idx >= 0)
    {
        // forward
        d1 = C->points[pI->t_idx + 1].planeTangent.distance(pI->pos);
        while ((pI->t_idx <= numBezierCache) && d1 >= 0.0f) {
            pI->t_idx++;
            d0 = d1;
            d1 = C->points[pI->t_idx + 1].planeTangent.distance(pI->pos);
        };
    }

    if ((pI->t_idx >= 0) && (pI->t_idx < numBezierCache))
    {
        // ok we pass the tangent plane test for this trapesium
        pI->d0 = C->points[pI->t_idx].planeInner.distance(pI->pos);
        pI->d1 = C->points[pI->t_idx].planeOuter.distance(pI->pos);

        if ((pI->d0 > 0) && (pI->d1 < 0.0f))
        {
            pI->UV.x = d0 / (d0 - d1); // since d1 is negative
            pI->UV.y = pI->d0 / (pI->d0 - pI->d1);
            pI->bHit = true;

            float h0 = C->points[pI->t_idx].A.y * (1.0f - pI->UV.x) + (C->points[pI->t_idx + 1].A.y) * pI->UV.x;
            float h1 = C->points[pI->t_idx].B.y * (1.0f - pI->UV.x) + (C->points[pI->t_idx + 1].B.y) * pI->UV.x;
            pI->bezierHeight = h0 * (1.0f - pI->UV.y) + h1 * pI->UV.y;

            pI->bRighthand = bezierBounding[C->bezierIndex].bRighthand;
            pI->roadGUID = bezierBounding[C->bezierIndex].roadGUID;
            pI->index = bezierBounding[C->bezierIndex].index;

            pI->UV.x = (pI->UV.x + pI->t_idx) / numBezierCache; // NOW FIX it up for the whole bezier

            return true;
        }
        else
        {
            pI->distanceTillNextSearch = __max(-pI->d0, pI->d1 - 10.0f);
            pI->bHit = false;
            return false;
        }
    }
    else
    {
        pI->bHit = false;
        pI->distanceTillNextSearch = __max(-d0, d1);
        return false;
    }
}



bool ODE_bezier::testBounds(bezierOneIntersection* pI)
{
    physicsBezier* pPhysics = &bezierBounding[pI->boundIndex];

    glm::vec2 diff = pI->pos - pPhysics->center;
    float length = glm::length(diff) - pPhysics->radius;

    if (length < 0)
    {
        float a = pPhysics->startPlane.distance(pI->pos);
        float b = pPhysics->endPlane.distance(pI->pos);
        if ((a >= 0) && (b < 0))
        {

            // ist true so make sure its cached
            {
                if (pPhysics->cacheIndex < 0) {
                    uint cacheSlot = cache.findEmpty();
                    // now break all teh existing links
                    if (cache.cache[cacheSlot].bezierIndex >= 0) {
                        bezierBounding[cache.cache[cacheSlot].bezierIndex].cacheIndex = -1;
                    }

                    // set new link and cache
                    cache.cache[cacheSlot].bezierIndex = pI->boundIndex;
                    cache.cacheSpline(pPhysics, cacheSlot);
                }
                pI->cacheIndex = pPhysics->cacheIndex;
                pI->t_idx = 0;
            }
            return true;
        }
        else
        {
            pI->distanceTillNextSearch = __max(-a, b);
            return false;
        }
    }
    else
    {
        pI->distanceTillNextSearch = length;		// this is for missed the bounding circle
        return false;
    }

}



void ODE_bezier::blendLayers(bezierIntersection* pI)
{
}



void ODE_bezier::setGrid(float size, uint numX, uint numY)
{
    gridLookup.allocate(size, numX, numY);
}



void ODE_bezier::clear()
{
    bezierBounding.clear();
    //cache.clear();
    needsReCache = true;
}



void ODE_bezier::buildGridFromBezier()
{
    for (auto& cell : gridLookup.buidData) {
        cell.clear();
    }
    //gridLookup.buidData.clear();
    uint size = (uint)bezierBounding.size();
    for (uint i = 0; i < size; i++)
    {
        gridLookup.insertBezier(bezierBounding[i].center, bezierBounding[i].radius, i);
    }
}


