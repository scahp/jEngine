#include "pch.h"
#include "jRHI.h"
#include "jShader.h"


//////////////////////////////////////////////////////////////////////////
void IUniformBuffer::Bind(const jShader* shader) const
{
	g_rhi->SetUniformbuffer(this, shader);
}

//////////////////////////////////////////////////////////////////////////

jRHI::jRHI()
{
}

void jRHI::MapBufferdata(IBuffer* buffer)
{

}
