#pragma once
#include "Falcor.h"

using namespace Falcor;


class computeShader
{
public:
	void load(const std::filesystem::path& _path);
	void dispatch(RenderContext* _renderContext, const uint32_t _width, const uint32_t _height, const uint32_t _slices=1);
    void dispatchIndirect(RenderContext* _renderContext, const Buffer* pArgBuffer, uint64_t argBufferOffset);
    void add(std::string _name, std::string _val) { defineList.add(_name, _val); }

	ComputeState::SharedPtr State() { return state; }
	ComputeVars::SharedPtr Vars() { return vars; }
	ComputeProgram::SharedPtr Program() { return program; }
    ProgramReflection::SharedConstPtr Reflector() { return program->getActiveVersion()->getReflector(); }

private:
    Program::DefineList         defineList;
	ComputeProgram::SharedPtr   program;
	ComputeState::SharedPtr     state;
	ComputeVars::SharedPtr      vars;
};
