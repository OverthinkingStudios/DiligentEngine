
#include"Sprites.h"

std::random_device rd;  //Will be used to obtain a seed for the random number engine
std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
std::uniform_real_distribution<float> dis11(-1, 1);
std::uniform_real_distribution<float> dis01(0, 1);

#define packedBuffer 60000
_spriteVertex_packed pV[packedBuffer];



_spriteVertex_packed spriteRender::pack(_spriteVertex v, bool cache = true)
{
	//if (cache)	packSunLayer(v);

	_spriteVertex_packed P;

	int iAO = (int)(v.AO * 255) & 0xff;
	int iW = (int)(v.width * 1024.0f);
	int iS = (int)(v.shadow * 255) & 0xff;

	P.pos = v.pos;
	P.up = v.up;
	P.idx = (v.u << 12) + (v.v << 8) + (v.du << 4) + (v.dv) + (v.bFreeRotate << 16) + +(v.bFadeout << 17);
	P.w = (iW << 16) + (iS << 8) + (iAO);

	pV[mN] = P;
	mN++;

	if (mN > 20000) {
		loadBlob();
	}
	return P;
}

void spriteRender::onLoad()
{
	DepthStencilState::Desc dsDesc;
    dsDesc.setDepthEnabled(false);
    dsDesc.setDepthWriteMask(false);
	mDepthStencil = DepthStencilState::create(dsDesc);
	
	Sampler::Desc samplerDesc;
	samplerDesc.setAddressingMode(Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap, Sampler::AddressMode::Wrap).setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear).setMaxAnisotropy(8);
	mpSampTrilinear = Sampler::create(samplerDesc);

	BlendState::Desc bsDesc;
	bsDesc.setRtBlend(0, true).setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::SrcAlpha, BlendState::BlendFunc::OneMinusSrcAlpha, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero);
	pAlphaBlendBS = BlendState::create(bsDesc);

	bsDesc.setRtBlend(0, true).setRtParams(0, BlendState::BlendOp::Add, BlendState::BlendOp::Add, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero, BlendState::BlendFunc::One, BlendState::BlendFunc::Zero);
	pNoBlendBS = BlendState::create(bsDesc);

	mDiff = Texture::createFromFile("sprites/sprite_diff.dds", false, true);
	mNorm = Texture::createFromFile("sprites/sprite_norm.dds", false, false);
	mTranslucent = Texture::createFromFile("sprites/sprite_trans.dds", false, true);
	//mDiff->generateMips();

	//csPlants.load("flight_SB_def.hlsl");

	uint32_t numPlants = 1024 * 1024 * 8;
	uint32_t numDynamic = 16384;
	uint32_t vertexCountPerInstance = 4;
	uint32_t zero = 0;

    struct vertex
    {
        float3 pos;
        uint idx;
        float3 up;
        uint w;
    };

	mpSB_static = Buffer::createStructured(sizeof(vertex), numPlants);
	mpSB_static->getUAVCounter()->setBlob(&numPlants, 0, sizeof(uint32_t));

	mpSB_dynamic[0] = Buffer::createStructured(sizeof(vertex), numDynamic);
	mpSB_dynamic[0]->getUAVCounter()->setBlob(&numDynamic, 0, sizeof(uint32_t));

	mpSB_dynamic[1] = Buffer::createStructured(sizeof(vertex), numDynamic);
	mpSB_dynamic[1]->getUAVCounter()->setBlob(&numDynamic, 0, sizeof(uint32_t));

	mpSB_dynamic[2] = Buffer::createStructured(sizeof(vertex), numDynamic);
	mpSB_dynamic[2]->getUAVCounter()->setBlob(&numDynamic, 0, sizeof(uint32_t));

    struct t_DrawArguments
    {
        uint vertexCountPerInstance;
        uint instanceCount;
        uint startVertexLocation;
        uint startInstanceLocation;
    };

	mpIndirectArgs_static = Buffer::createStructured(sizeof(t_DrawArguments), 101, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
	mpIndirectArgs_static->setBlob(&vertexCountPerInstance, 0, sizeof(uint32_t));
	mpIndirectArgs_static->setBlob(&zero, 8, sizeof(uint32_t));
	mpIndirectArgs_static->setBlob(&zero, 12, sizeof(uint32_t));

	mpIndirectArgs_dynamic[0] = Buffer::createStructured(sizeof(t_DrawArguments), 101, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
	mpIndirectArgs_dynamic[0]->setBlob(&vertexCountPerInstance, 0, sizeof(uint32_t));
	mpIndirectArgs_dynamic[0]->setBlob(&zero, 8, sizeof(uint32_t));
	mpIndirectArgs_dynamic[0]->setBlob(&zero, 12, sizeof(uint32_t));

	mpIndirectArgs_dynamic[1] = Buffer::createStructured(sizeof(t_DrawArguments), 101, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
	mpIndirectArgs_dynamic[1]->setBlob(&vertexCountPerInstance, 0, sizeof(uint32_t));
	mpIndirectArgs_dynamic[1]->setBlob(&zero, 8, sizeof(uint32_t));
	mpIndirectArgs_dynamic[1]->setBlob(&zero, 12, sizeof(uint32_t));

	mpIndirectArgs_dynamic[2] = Buffer::createStructured(sizeof(t_DrawArguments), 101, Resource::BindFlags::UnorderedAccess | Resource::BindFlags::IndirectArg);
	mpIndirectArgs_dynamic[2]->setBlob(&vertexCountPerInstance, 0, sizeof(uint32_t));
	mpIndirectArgs_dynamic[2]->setBlob(&zero, 8, sizeof(uint32_t));
	mpIndirectArgs_dynamic[2]->setBlob(&zero, 12, sizeof(uint32_t));

	plantShaderPlants.load("Samples/Earthworks_4/hlsl/render_sprite.hlsl", "vsMain", "psMain", Vao::Topology::TriangleStrip);
	plantShaderPlants.Vars()->setBuffer("VB", mpSB_static);
	plantShaderPlants.Vars()->setTexture("gTex", mDiff);
	plantShaderPlants.Vars()->setTexture("gNorm", mNorm);
	plantShaderPlants.Vars()->setTexture("gTranclucent", mTranslucent);
	plantShaderPlants.Vars()->setSampler("gSampler", mpSampTrilinear);
}

void spriteRender::onRender(Camera::SharedPtr _camera, RenderContext* _renderContext, Fbo::SharedPtr _fbo, GraphicsState::Viewport _viewport, Texture::SharedPtr mcubeWorld)
{
    glm::mat4 V = toGLM(_camera->getViewMatrix());
    glm::mat4 P = toGLM(_camera->getProjMatrix());
    glm::mat4 VP = toGLM(_camera->getViewProjMatrix());
    rmcv::mat4 view, proj, viewproj;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            view[j][i] = V[j][i];
            proj[j][i] = P[j][i];
            viewproj[j][i] = VP[j][i];
        }
    }

    plantShaderPlants.State()->setFbo(_fbo);
	plantShaderPlants.Vars()["gConstantBuffer"]["view"] = view;
	plantShaderPlants.Vars()["gConstantBuffer"]["proj"] = proj;
	plantShaderPlants.Vars()["gConstantBuffer"]["eye"] = _camera->getPosition();
	plantShaderPlants.Vars()["gConstantBuffer"]["alpha_pass"] = 0;
	plantShaderPlants.Vars()["gConstantBuffer"]["start_index"] = 0;
	//plantShaderPlants.getVars()->setTexture("gCube", mcubeWorld);
	plantShaderPlants.Vars()->setBuffer("VB", mpSB_static);
	plantShaderPlants.renderIndirect(_renderContext, mpIndirectArgs_static);

    plantShaderPlants.State()->setViewport(0, _viewport, true);
	
	
	
	
	plantShaderPlants.State()->setDepthStencilState(mDepthStencil);

	// in and out
	for (int y = 0; y < 10; y++)
	{
		for (int x = 0; x < 10; x++)
		{
			mBlocks[y][x].bVis = !_camera->isObjectCulled(mBlocks[y][x].BB);
		}
	}
	
	for (int y = 0; y < 10; y++)
	{
		for (int x = 0; x < 10; x++)
		{
			if (mBlocks[y][x].bVis)
			{
				plantShaderPlants.Vars()["gConstantBuffer"]["start_index"] = mBlocks[y][x].startVertex;
				plantShaderPlants.renderIndirect(_renderContext, mpIndirectArgs_static, pAlphaBlendBS, y * 10 + x + 1, 1);
			}
		}
	}
	
	plantShaderPlants.Vars()["gConstantBuffer"]["alpha_pass"] = 1;
	//plantShaderPlants.renderIndirect(RC, fbo, mpIndirectArgs_plants, pAlphaBlendBS);
	for (int y = 0; y < 10; y++)
	{
		for (int x = 0; x < 10; x++)
		{
			if (mBlocks[y][x].bVis)
			{
				plantShaderPlants.Vars()["gConstantBuffer"]["start_index"] = mBlocks[y][x].startVertex;
				plantShaderPlants.renderIndirect(_renderContext, mpIndirectArgs_static, pAlphaBlendBS, y * 10 + x + 1, 1);
			}
		}
	}
	
	
	// and now for the dynamic buffer
	
	plantShaderPlants.Vars()["gConstantBuffer"]["alpha_pass"] = 0;
	plantShaderPlants.Vars()["gConstantBuffer"]["start_index"] = 0;
	plantShaderPlants.Vars()->setBuffer("VB", mpSB_dynamic[dynamicIndex]);
	plantShaderPlants.renderIndirect(_renderContext, mpIndirectArgs_dynamic[dynamicIndex], pAlphaBlendBS, 0);

    plantShaderPlants.Vars()["gConstantBuffer"]["alpha_pass"] = 1;
	plantShaderPlants.renderIndirect(_renderContext, mpIndirectArgs_dynamic[dynamicIndex], pAlphaBlendBS, 0);
	
}




void spriteRender::loadBlob()
{
	mpSB_static->setBlob((void*)pV, sizeof(_spriteVertex_packed) *mCNT, sizeof(_spriteVertex_packed) * mN);
	mCNT += mN;
	mN = 0;
}

void spriteRender::loadBlobDynamic()
{
	dynamicIndex += 1;
	if (dynamicIndex > 2) dynamicIndex = 0;
	mpSB_dynamic[dynamicIndex]->setBlob((void*)pV, sizeof(_spriteVertex_packed) *mCNT, sizeof(_spriteVertex_packed) * mN);
	mCNT += mN;
	mN = 0;
}




void spriteRender::loadSV(_spriteVertex *pSV, glm::vec3 pos, glm::vec3 up, float width, UCHAR u, UCHAR v, CHAR du, UCHAR dv, float AO, float shadow, bool bFreeRotate)
{
	pSV->pos = pos;
	pSV->up = up;
	pSV->width = width;
	pSV->u = u;
	pSV->v = v;
	pSV->du = du;
	pSV->dv = dv;
	pSV->AO = AO;
	pSV->shadow = shadow;
	pSV->bFreeRotate = bFreeRotate;
	pSV->bFadeout = false;
}

void spriteRender::loadSV_start_end_PACK(_spriteVertex *pSV, glm::vec3 start, glm::vec3 end, float width, UCHAR u, UCHAR v, CHAR du, UCHAR dv, float AO, float shadow, bool bFreeRotate)
{
	pSV->pos = (start+end)*0.5f;
	pSV->up = (end-start);
	pSV->width = width;
	pSV->u = u;
	pSV->v = v;
	pSV->du = du;
	pSV->dv = dv;
	pSV->AO = AO;
	pSV->shadow = shadow;
	pSV->bFreeRotate = bFreeRotate;

	pack(*pSV);
}





void spriteRender::loadBinary(const char*name, glm::vec3 pos, float scale, float rotation, bool loadLeave )
{
    glm::mat4 MAT(1.0f);
		
    glm::mat4 r= glm::rotate(MAT, rotation, glm::vec3(0, 1, 0));


    glm::mat4 t = glm::translate(MAT, pos);
				 t = glm::rotate(t, rotation, glm::vec3(0, 1, 0));


                 glm::mat4 s = glm::scale(MAT, glm::vec3(scale, scale, scale));

		FILE *f = fopen(name, "rb");
		if (f)
		{
				uint num;
				_spriteVertex V;

				// branches --------------------------------------------------------------------------------------
				fread(&num, 1, sizeof(uint), f);
				for (uint i = 0; i < num; i++)
				{
						fread(&V, 1, sizeof(_spriteVertex), f);
						// FIXME scale and roatate
						V.pos = t * s * glm::vec4(V.pos, 1);
						V.up = r * s * glm::vec4(V.up, 1);
						V.width *= scale;
						pack(V);
				}

				// leaves --------------------------------------------------------------------------------------
				if (loadLeave)
				{
						fread(&num, 1, sizeof(uint), f);
						for (uint i = 0; i < num; i++)
						{
								fread(&V, 1, sizeof(_spriteVertex), f);
								// FIXME scale and roatate
								V.pos = t * s * glm::vec4(V.pos, 1);
								V.up = r * s * glm::vec4(V.up, 1) * 1.0f;
								V.width *= 1.0f * scale;
							//	V.u = 0;		// change texture
								pack(V);
						}
				}


				// low res fading leaves --------------------------------------------------------------------------------------
				if (loadLeave)
				{
						fread(&num, 1, sizeof(uint), f);
						for (uint i = 0; i < num; i++)
						{
								fread(&V, 1, sizeof(_spriteVertex), f);
								// FIXME scale and roatate
								V.pos = t * s * glm::vec4(V.pos, 1);
								V.up = r * s * glm::vec4(V.up, 1) * 1.0f;
								V.width *= 1.0f * scale;
							//	V.u = 0;		// change texture
								pack(V);
						}
				}

				fclose(f);
		}
}





void spriteRender::clearDynamic(){
	mCNT = 0;
	mN = 0;
}

void spriteRender::pushMarker(float3 pos, uint type, float size)
{
	_spriteVertex SV;
	loadSV(&SV, pos, float3(0, size, 0), size, 2+type, 15, 1, 1, 1.0f, 1.0f, true);
	pack(SV);
}

void spriteRender::pushLine(float3 start, float3 end, int type, float width)
{
	_spriteVertex SV;
	loadSV(&SV, (start+end)*0.5f, (end - start), width, 15, 15-type, 1, 1, 1.0f, 1.0f, false);
	pack(SV);
}

void spriteRender::loadDynamic()
{
	loadBlobDynamic();

	mpIndirectArgs_dynamic[dynamicIndex]->setBlob(&mCNT, 4, sizeof(uint32_t));
	//mpIndirectArgs_dynamic[dynamicIndex]->uploadToGPU(0, 16);
	//mpSB_dynamic[dynamicIndex]->getUAVCounter()->updateData(&mCNT, 0, sizeof(uint32_t));
}


void spriteRender::loadStatic()
{
	loadBlob();

	mpIndirectArgs_static->setBlob(&mCNT, 4, sizeof(uint32_t));
	//mpIndirectArgs_static->uploadToGPU(0, 16);
	//mpSB_static->getUAVCounter()->updateData(&mCNT, 0, sizeof(uint32_t));
}











void spriteRender::loadPackfile(const char* filename, const char* blockname)
{
	uint32_t vertexCountPerInstance = 4;
	uint32_t zero = 0;

	FILE *ftxt = fopen(blockname, "rb");
	if (ftxt)
	{
		fread(mBlocks, sizeof(_spriteBlock), 10 * 10, ftxt);
		fclose(ftxt);

		for (int y = 0; y < 10; y++)
		{
			for (int x = 0; x < 10; x++)
			{
				//mBlocks[y][x].startVertex *= sizeof(_spriteVertex_packed);
				//mBlocks[y][x].pIndArgs = StructuredBuffer::create(csPlants.getProgram(), "drawArgs", 1, Resource::BindFlags::IndirectArg);
				size_t off = (y * 10 + x + 1) * 16;
				mpIndirectArgs_static->setBlob(&vertexCountPerInstance,		off + 0, sizeof(uint32_t));
				mpIndirectArgs_static->setBlob(&mBlocks[y][x].Size,			off + 4, sizeof(uint32_t));
				mpIndirectArgs_static->setBlob(&zero,						off + 8, sizeof(uint32_t));
				mpIndirectArgs_static->setBlob(&zero,						off + 12, sizeof(uint32_t));
				//mpIndirectArgs_plants->uploadToGPU(0, 16);
                glm::vec3 bbMin = mBlocks[y][x].center - glm::vec3(250, 0, 250);
                glm::vec3 bbMax = mBlocks[y][x].center + glm::vec3(250, 40, 250);
				mBlocks[y][x].BB = AABB(bbMin, bbMax);
				//mBlocks[y][x].BB.center.center = mBlocks[y][x].center;
				//mBlocks[y][x].BB.extent = vec3(m_blockSize/2, 40, m_blockSize/2);
			}
		}
	}

	FILE *f = fopen(filename, "rb");
	if (f)
	{
		int cnt = 0;
		int blocks, res;
		fread(&cnt, sizeof(int), 1, f);

		blocks = (int)floor((double)cnt / packedBuffer);
		res = cnt - (blocks * packedBuffer);

		mCNT = 0;		// clear buffer

		for (int i = 0; i < blocks; i++)
		{
			fread(pV, sizeof(_spriteVertex_packed), packedBuffer, f);
			mN = packedBuffer;
			loadBlob();
		}

		fread(pV, sizeof(_spriteVertex_packed), res, f);
		mN = res;
		loadBlob();

		mpIndirectArgs_static->setBlob(&mCNT, 4, sizeof(uint32_t));
		//mpIndirectArgs_static->uploadToGPU(0, 16*101);
		//mpSB_static->getUAVCounter()->updateData(&mCNT, 0, sizeof(uint32_t));

		fclose(f);
	}
}

void spriteRender::sortIntoBlocks()
{
    glm::vec2 start;
    glm::vec2 end;
	int blockStart = 0;
	for (int y = 0; y < 10; y++)
	{
		for (int x = 0; x < 10; x++)
		{
			m_BlockSprites[y][x].clear();
			start = glm::vec2((x-5)*m_blockSize, (y-5)*m_blockSize);
			end = start + glm::vec2(m_blockSize, m_blockSize);
			int sCNT = 0;
			for (int layer = 0; layer < 99; layer++)
			{
				for (int l = 0; l < sunlayers[layer].size(); l++)
				{
					sCNT++;
					_spriteVertex v = sunlayers[layer][l];
                    glm::vec3 test = v.pos;
					if (test.x < -1000) test.x = -1000;
					if (test.x >= 1000) test.x = 999;
					if (test.z < -1000) test.z = -1000;
					if (test.z >= 1000) test.z = 999;
					if ((test.x >= start.x) && (test.x < end.x) && (test.z >= start.y) && (test.z < end.y))
					{
						m_BlockSprites[y][x].push_back(v);
					}
				}
			}
			mBlocks[y][x].startVertex = blockStart;
			mBlocks[y][x].Size = (int)m_BlockSprites[y][x].size();
			mBlocks[y][x].radius = 900.0f;  // so iets
			mBlocks[y][x].center = glm::vec3((start.x + end.x) / 2, 0, (start.y + end.y) / 2);
			//vec3 bbMin = vec3(start.x, 0, start.y);
			//vec3 bbMax = vec3(end.x, 40, end.y);
            mBlocks[y][x].BB = AABB(float3(start.x, 0, start.y), float3(end.x, 40, end.y));
			blockStart += (int)m_BlockSprites[y][x].size();
		}
	}
}
