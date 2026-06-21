

#include "pixelShader.h"

void pixelShader::remove(std::string _name) { 
	defineList.remove(_name); 
}

void pixelShader::add(std::string _name, std::string _val) { 
	defineList.add(_name, _val);
}

void pixelShader::load(const std::filesystem::path& _path, const std::string _vsEntry, const std::string _psEntry, Vao::Topology _topology, const std::string _gsEntry)
{
    if (_gsEntry.size() > 0) {
        GraphicsProgram::Desc d(_path);
        d.vsEntry(_vsEntry).psEntry(_psEntry).gsEntry(_gsEntry);
        d.setShaderModel("6_5");
        program = GraphicsProgram::create(d, defineList);
    }
    else {
        program = GraphicsProgram::createFromFile(_path, _vsEntry, _psEntry, defineList);
    }
	reflection = program->getActiveVersion()->getReflector();

	state = GraphicsState::create();
	state->setProgram(program);

	vars = GraphicsVars::create(reflection);

	//Create empty vbo for draw 
	VertexLayout::SharedPtr pLayout = VertexLayout::create();
	std::array<unsigned short, 128*6> ibData;
	for (int i = 0; i < 128; i++) {
		ibData[i * 6 + 0] = 0 + i * 2;
		ibData[i * 6 + 1] = 1 + i * 2;
		ibData[i * 6 + 2] = 2 + i * 2;

		ibData[i * 6 + 3] = 1 + i * 2;
		ibData[i * 6 + 4] = 3 + i * 2;
		ibData[i * 6 + 5] = 2 + i * 2;
	}
	Buffer::SharedPtr pIB = Buffer::create(128 * 6 * 2, Buffer::BindFlags::Index, Buffer::CpuAccess::Write, ibData.data());
	state->setVao(Vao::create(_topology, pLayout, bufferVec, pIB, ResourceFormat::R16Uint));
}



void pixelShader::renderIndirect(RenderContext* _renderContext, Buffer::SharedPtr _indirectArgs, BlendState::SharedPtr BS, uint _startArg, uint _numArgs)
{
	//vars->setBuffer("gConstantBuffer", constantBuffer);
	if (BS) state->setBlendState(BS);
    _renderContext->drawIndirect(state.get(), vars.get(), _numArgs, _indirectArgs.get(), _startArg*16, nullptr, 0);
}



void pixelShader::drawIndexedInstanced(RenderContext* _renderContext, uint32_t _index, uint32_t _instance)
{
    //vars->setBuffer("gConstantBuffer", constantBuffer);
    _renderContext->drawIndexedInstanced(state.get(), vars.get(), _index, _instance, 0, 0, 0);
}


void pixelShader::drawInstanced(RenderContext* _renderContext, uint32_t _vcount, uint32_t _instance)
{
    _renderContext->drawInstanced(state.get(), vars.get(), _vcount, _instance, 0, 0);
}
