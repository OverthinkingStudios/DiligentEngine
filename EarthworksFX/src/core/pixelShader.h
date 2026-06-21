#pragma once
#include "FalcorCompat.hpp"

using namespace Falcor;

class pixelShader
{
public:
	void load(const std::filesystem::path& _path, const std::string _vsEntry, const std::string _psEntry, Vao::Topology _topology, const std::string _gsEntry = "");                                                  // "RenderPasses/SkyBox/SkyBox.3d.slang", "vsMain", "psMain");
	void renderIndirect(RenderContext* _renderContext, Buffer::SharedPtr _indirectArgs, BlendState::SharedPtr BS = nullptr, uint _startArg = 0, uint _numArgs = 1);
	void drawIndexedInstanced(RenderContext* _renderContext, uint32_t _index, uint32_t _instance);
    void drawInstanced(RenderContext* _renderContext, uint32_t _index, uint32_t _instance);
    void setFbo(Fbo::SharedPtr _fbo) { state->setFbo(_fbo); }
    void add(std::string _name, std::string _val) { defineList.add(_name, _val); }

	const GraphicsProgram::SharedPtr Program() { return program; }
    Buffer::SharedPtr ConstantBuffer() { return constantBuffer; }
	GraphicsState::SharedPtr State() { return state; }
	GraphicsVars::SharedPtr Vars() { return vars; }

private:
	Program::DefineList                 defineList;
	GraphicsProgram::SharedPtr          program;
	ProgramReflection::SharedConstPtr   reflection;
    Buffer::SharedPtr                   constantBuffer;
	GraphicsVars::SharedPtr             vars;
	GraphicsState::SharedPtr            state;
	Vao::SharedPtr                      vao;
	Vao::BufferVec                      bufferVec;
};
