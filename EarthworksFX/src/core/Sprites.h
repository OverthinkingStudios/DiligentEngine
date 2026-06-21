#pragma once

#include "sprite_defines.h"
#include "computeShader.h"
#include "pixelShader.h"
#include <random>
#include <vector>

//using namespace std;

// ???
// So maybe having multiple shaders ere is faster , one for solid and on for alpha
// and since the sprite buffer is bound to theshader, if we even need more, then maybe more shaders
// FIXME on LOADcreate perfect sized structured buffer ;-)
struct _spriteBlock
{
	Buffer::SharedPtr pIndArgs;
	glm::vec3 center;
	float radius;	// for frustom culling
	uint32_t startVertex;
	uint32_t Size;		// duplicates IndArgs but makes it easirt I think
	Falcor::AABB	BB;
	bool bVis;
};

typedef std::vector<_spriteVertex> _sunLayer;




class spriteRender
{
public:
	spriteRender() { ; }
	virtual ~spriteRender() { ; }

	_spriteVertex_packed pack(_spriteVertex v, bool cache);
	void onLoad();
	void onRender(Camera::SharedPtr camera, RenderContext* _renderContext, Fbo::SharedPtr fbo, GraphicsState::Viewport _viewport, Texture::SharedPtr	mcubeWorld);

	void loadBlob();
	void loadSV(_spriteVertex *pSV, glm::vec3 pos, glm::vec3 up, float width, UCHAR u, UCHAR v, CHAR du, UCHAR dv, float AO, float shadow, bool bFreeRotate);
	void loadSV_start_end_PACK(_spriteVertex *pSV, glm::vec3 start, glm::vec3 end, float width, UCHAR u, UCHAR v, CHAR du, UCHAR dv, float AO, float shadow, bool bFreeRotate);
	
	
	// dynamic section ---------------------------------------
	void loadBlobDynamic();
	void clearDynamic();
	void pushMarker(float3 pos, uint type, float size = 1.0);
	void pushLine(float3 start, float3 end, int type, float width = 1.0f);
	void loadDynamic();
	void loadStatic();


	void loadBinary(const char*name, glm::vec3 pos, float scale, float rotation, bool loadLeave = true);

	

	void loadPackfile(const char* filename, const char* blockname);


	_sunLayer sunlayers[100];
	

private:
	Buffer::SharedPtr mpSB_static;
	Buffer::SharedPtr mpIndirectArgs_static;
	pixelShader					plantShaderPlants;
	computeShader				csPlants;
	Sampler::SharedPtr			mpSampTrilinear;
	BlendState::SharedPtr		pAlphaBlendBS;
	BlendState::SharedPtr		pNoBlendBS;

	DepthStencilState::SharedPtr	mDepthStencil;

	Texture::SharedPtr mDiff;
	Texture::SharedPtr mNorm;
	Texture::SharedPtr mSpec;
	Texture::SharedPtr mTranslucent;

	Buffer::SharedPtr mpSB_dynamic[3];
	int dynamicIndex = 0;
	Buffer::SharedPtr mpIndirectArgs_dynamic[3];

	// for building the worlds
	UINT				mCNT = 0;
	UINT				mN = 0;
    glm::vec2 mapStart;
    glm::vec2 mapSize;
    glm::vec2 dataSize;
    glm::vec3 pointStart;
	

	// FRUMTUM Culling
public:
	_spriteBlock	mBlocks[10][10];
	void	sortIntoBlocks();
	float	m_blockSize = 200.0f;
	_sunLayer m_BlockSprites[10][10];
};
