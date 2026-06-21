
#include "computeShader.h"


void computeShader::load(const std::filesystem::path& _path)
{
    Program::DefineList defines;
    defines.add("CHUNK_SIZE", "256");   // Dummy values just so we can get reflection data. We'll set the actual values in execute().

	program = ComputeProgram::createFromFile(_path, "main", defines);
	state = ComputeState::create();
	state->setProgram( program );
	vars = ComputeVars::create( program.get() );
}

void computeShader::dispatch(RenderContext* _renderContext, const uint32_t _width, const uint32_t _height, const uint32_t _slices)
{
	_renderContext->dispatch(state.get(), vars.get(), uint3(_width, _height, _slices));
}

void computeShader::dispatchIndirect(RenderContext* _renderContext, const Buffer* pArgBuffer, uint64_t argBufferOffset)
{
    _renderContext->dispatchIndirect(state.get(), vars.get(), pArgBuffer, argBufferOffset);
}
