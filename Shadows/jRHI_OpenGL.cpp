#include "pch.h"
#include "jRHI_OpenGL.h"
#include "jFile.h"
#include "jRHIType.h"
#include "jShader.h"

jRHI_OpenGL::jRHI_OpenGL()
{
}


jRHI_OpenGL::~jRHI_OpenGL()
{
}

jVertexBuffer* jRHI_OpenGL::CreateVertexBuffer(const std::shared_ptr<jVertexStreamData>& streamData)
{
	if (!streamData)
		return nullptr;

	jVertexBuffer_OpenGL* vertexBuffer = new jVertexBuffer_OpenGL();
	vertexBuffer->VertexStreamData = streamData;
	glGenVertexArrays(1, &vertexBuffer->VAO);
	glBindVertexArray(vertexBuffer->VAO);
	std::list<uint32> buffers;
	for (const auto& iter : streamData->Params)
	{
		if (iter->Stride <= 0)
			continue;

		unsigned int bufferType = GL_STATIC_DRAW;
		switch (iter->BufferType)
		{
		case EBufferType::STATIC:
			bufferType = GL_STATIC_DRAW;
			break;
		case EBufferType::DYNAMIC:
			bufferType = GL_DYNAMIC_DRAW;
			break;
		}

		jVertexStream_OpenGL stream;
		glGenBuffers(1, &stream.BufferID);
		glBindBuffer(GL_ARRAY_BUFFER, stream.BufferID);
		glBufferData(GL_ARRAY_BUFFER, iter->GetBufferSize(), iter->GetBufferData(), bufferType);
		stream.Name = iter->Name;
		stream.Count = iter->Stride / iter->ElementTypeSize;
		stream.BufferType = EBufferType::STATIC;
		stream.ElementType = iter->ElementType;
		stream.Stride = iter->Stride;
		stream.Offset = 0;

		vertexBuffer->Streams.emplace_back(stream);
	}

	return vertexBuffer;
}

jIndexBuffer* jRHI_OpenGL::CreateIndexBuffer(const std::shared_ptr<jIndexStreamData>& streamData)
{
	if (!streamData)
		return nullptr;

	unsigned int bufferType = GL_STATIC_DRAW;
	switch (streamData->Param->BufferType)
	{
	case EBufferType::STATIC:
		bufferType = GL_STATIC_DRAW;
		break;
	case EBufferType::DYNAMIC:
		bufferType = GL_DYNAMIC_DRAW;
		break;
	}

	jIndexBuffer_OpenGL* indexBuffer = new jIndexBuffer_OpenGL();
	indexBuffer->IndexStreamData = streamData;
	std::list<uint32> buffers;
	glGenBuffers(1, &indexBuffer->BufferID);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, indexBuffer->BufferID);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, streamData->Param->GetBufferSize(), streamData->Param->GetBufferData(), bufferType);

	return indexBuffer;
}

void jRHI_OpenGL::BindVertexBuffer(const jVertexBuffer* vb, const jShader* shader)
{
	auto vb_gl = static_cast<const jVertexBuffer_OpenGL*>(vb);
	glBindVertexArray(vb_gl->VAO);
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	for (const auto& iter : vb_gl->Streams)
	{
		auto loc = glGetAttribLocation(shader_gl->program, iter.Name.c_str());
		if (loc != -1)
		{
			unsigned int elementType = 0;
			switch (iter.ElementType)
			{
			case EBufferElementType::BYTE:
				elementType = GL_UNSIGNED_BYTE;
				break;
			case EBufferElementType::INT:
				elementType = GL_UNSIGNED_INT;
				break;
			case EBufferElementType::FLOAT:
				elementType = GL_FLOAT;
				break;
			}

			glBindBuffer(GL_ARRAY_BUFFER, iter.BufferID);
			glVertexAttribPointer(loc, iter.Count, elementType, iter.Normalized, iter.Stride, reinterpret_cast<const void*>(iter.Offset));
			glEnableVertexAttribArray(loc);
		}
	}
}

void jRHI_OpenGL::BindIndexBuffer(const jIndexBuffer* ib, const jShader* shader)
{
	auto ib_gl = static_cast<const jIndexBuffer_OpenGL*>(ib);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib_gl->BufferID);
}

void jRHI_OpenGL::MapBufferdata(IBuffer* buffer)
{
	throw std::logic_error("The method or operation is not implemented.");
}

void jRHI_OpenGL::DrawArray(EPrimitiveType type, int vertStartIndex, int vertCount)
{
	glDrawArrays(GetPrimitiveType(type), vertStartIndex, vertCount);
}

void jRHI_OpenGL::DrawElement(EPrimitiveType type, int elementSize, int32 startIndex /*= -1*/, int32 count /*= -1*/)
{
	const auto elementType = (elementSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
	glDrawElements(GetPrimitiveType(type), count, elementType, reinterpret_cast<void*>(startIndex * elementSize));
}

void jRHI_OpenGL::DispatchCompute(uint32 numGroupsX, uint32 numGroupsY, uint32 numGroupsZ)
{
	glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
}

void jRHI_OpenGL::EnableDepthBias(bool enable)
{
	if (enable)
		glEnable(GL_POLYGON_OFFSET_FILL);
	else
		glDisable(GL_POLYGON_OFFSET_FILL);
}

void jRHI_OpenGL::SetDepthBias(float constant, float slope)
{
	glPolygonOffset(1.0f, 1.0f);
}

void jRHI_OpenGL::DrawElementBaseVertex(EPrimitiveType type, int elementSize, int32 startIndex, int32 count, int32 baseVertexIndex)
{
	const auto elementType = (elementSize == 4) ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
	glDrawElementsBaseVertex(GetPrimitiveType(type), count, elementType, reinterpret_cast<void*>(startIndex * elementSize), baseVertexIndex);
}

void jRHI_OpenGL::EnableSRGB(bool enable)
{
	if (enable)
		glEnable(GL_FRAMEBUFFER_SRGB);
	else
		glDisable(GL_FRAMEBUFFER_SRGB);
}

IShaderStorageBufferObject* jRHI_OpenGL::CreateShaderStorageBufferObject(const char* blockname) const
{
	auto ssbo = new jShaderStorageBufferObject_OpenGL(blockname);
	ssbo->Init();
	return ssbo;
}

IAtomicCounterBuffer* jRHI_OpenGL::CreateAtomicCounterBuffer(const char* name, int32 bindingPoint) const
{
	auto acbo = new jAtomicCounterBuffer_OpenGL(name, bindingPoint);
	acbo->Init();
	return acbo;
}

void jRHI_OpenGL::SetViewport(int32 x, int32 y, int32 width, int32 height) const
{
	glViewport(x, y, width, height);
}

void jRHI_OpenGL::SetViewport(const jViewport& viewport) const
{
	glViewport(static_cast<int32>(viewport.x), static_cast<int32>(viewport.y), static_cast<int32>(viewport.width), static_cast<int32>(viewport.height));
}

void jRHI_OpenGL::SetViewportIndexed(int32 index, float x, float y, float width, float height) const
{
	glViewportIndexedf(index, x, y, width, height);
}

void jRHI_OpenGL::SetViewportIndexed(int32 index, const jViewport& viewport) const
{
	glViewportIndexedfv(index, reinterpret_cast<const float*>(&viewport));
}

void jRHI_OpenGL::SetViewportIndexedArray(int32 startIndex, int32 count, const jViewport* viewports) const
{
	glViewportArrayv(startIndex, count, reinterpret_cast<const float*>(viewports));
}

void jRHI_OpenGL::EnableDepthClip(bool enable)
{
	// Enabling GL_DEPTH_CLAMP means that it disabling depth clip.
	if (enable)
		glDisable(GL_DEPTH_CLAMP);
	else
		glEnable(GL_DEPTH_CLAMP);
}

void jRHI_OpenGL::SetClear(ERenderBufferType typeBit)
{
	uint32 clearBufferBit = 0;
	uint32 bit = static_cast<uint32>(typeBit);
	if (static_cast<uint32>(ERenderBufferType::COLOR) & bit)
		clearBufferBit |= GL_COLOR_BUFFER_BIT;
	if (static_cast<uint32>(ERenderBufferType::DEPTH) & bit)
		clearBufferBit |= GL_DEPTH_BUFFER_BIT;
	if (static_cast<uint32>(ERenderBufferType::STENCIL) & bit)
		clearBufferBit |= GL_STENCIL_BUFFER_BIT;

	glClear(clearBufferBit);
}

void jRHI_OpenGL::SetClearColor(float r, float g, float b, float a)
{
	glClearColor(r, g, b, a);
}

void jRHI_OpenGL::SetClearColor(Vector4 rgba)
{
	glClearColor(rgba.x, rgba.y, rgba.z, rgba.w);
}

void jRHI_OpenGL::SetShader(const jShader* shader)
{
	JASSERT(shader);
	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	glUseProgram(shader_gl->program);
}

jShader* jRHI_OpenGL::CreateShader(const jShaderInfo& shaderInfo)
{
	// https://stackoverflow.com/questions/5878775/how-to-find-and-replace-string
	auto ReplaceString = [](std::string subject, const std::string& search, const std::string& replace) {
			size_t pos = 0;
			while ((pos = subject.find(search, pos)) != std::string::npos) {
				subject.replace(pos, search.length(), replace);
				pos += replace.length();
			}
			return subject;
	};

	static bool initializedCommonShader = false;
	static std::string commonShader;
	static std::string shadowShader;
	if (!initializedCommonShader)
	{
		initializedCommonShader = true;

		jFile commonFile;
		commonFile.OpenFile("Shaders/common.glsl", FileType::TEXT, ReadWriteType::READ);
		commonFile.ReadFileToBuffer(false);
		commonShader = commonFile.GetBuffer();

		jFile shadowFile;
		shadowFile.OpenFile("Shaders/shadow.glsl", FileType::TEXT, ReadWriteType::READ);
		shadowFile.ReadFileToBuffer(false);
		shadowShader = shadowFile.GetBuffer();
	}

	uint32 program = 0;
	if (shaderInfo.cs.length())
	{
		jFile csFile;
		csFile.OpenFile(shaderInfo.cs.c_str(), FileType::TEXT, ReadWriteType::READ);
		csFile.ReadFileToBuffer(false);
		std::string csText(csFile.GetBuffer());

		auto compute_shader = glCreateShader(GL_COMPUTE_SHADER);
		const char* vsPtr = csText.c_str();
		glShaderSource(compute_shader, 1, &vsPtr, nullptr);
		glCompileShader(compute_shader);
		int isValid = 0;
		glGetShaderiv(compute_shader, GL_COMPILE_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetShaderiv(compute_shader, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::vector<char> errorLog(maxLength + 1, 0);
				glGetShaderInfoLog(compute_shader, maxLength, &maxLength, &errorLog[0]);
				JMESSAGE(&errorLog[0]);
				glDeleteShader(compute_shader);
			}
			return nullptr;
		}

		program = glCreateProgram();
		glAttachShader(program, compute_shader);
		glLinkProgram(program);
		isValid = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::vector<char> errorLog(maxLength + 1, 0);
				glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
				JMESSAGE(&errorLog[0]);
				glDeleteShader(compute_shader);
				glDeleteProgram(program);
			}
			return nullptr;
		}

		glDeleteShader(compute_shader);
	}
	else
	{
		jFile vsFile;
		vsFile.OpenFile(shaderInfo.vs.c_str(), FileType::TEXT, ReadWriteType::READ);
		vsFile.ReadFileToBuffer(false);
		std::string vsText(vsFile.IsBufferEmpty() ? "" : vsFile.GetBuffer());
		vsText = ReplaceString(vsText, "#include \"shadow.glsl\"", shadowShader);
		vsText = ReplaceString(vsText, "#include \"common.glsl\"", commonShader);
		vsText = ReplaceString(vsText, "#preprocessor", shaderInfo.vsPreProcessor);

		jFile gsFile;
		gsFile.OpenFile(shaderInfo.gs.c_str(), FileType::TEXT, ReadWriteType::READ);
		gsFile.ReadFileToBuffer(false);
		std::string gsText(gsFile.IsBufferEmpty() ? "" : gsFile.GetBuffer());
		gsText = ReplaceString(gsText, "#include \"shadow.glsl\"", shadowShader);
		gsText = ReplaceString(gsText, "#include \"common.glsl\"", commonShader);
		gsText = ReplaceString(gsText, "#preprocessor", shaderInfo.gsPreProcessor);

		jFile fsFile;
		fsFile.OpenFile(shaderInfo.fs.c_str(), FileType::TEXT, ReadWriteType::READ);
		fsFile.ReadFileToBuffer(false);
		std::string fsText(fsFile.IsBufferEmpty() ? "" : fsFile.GetBuffer());
		fsText = ReplaceString(fsText, "#include \"shadow.glsl\"", shadowShader);
		fsText = ReplaceString(fsText, "#include \"common.glsl\"", commonShader);
		fsText = ReplaceString(fsText, "#preprocessor", shaderInfo.fsPreProcessor);

		auto vs = glCreateShader(GL_VERTEX_SHADER);
		const char* vsPtr = vsText.c_str();
		glShaderSource(vs, 1, &vsPtr, nullptr);
		glCompileShader(vs);

		auto checkShaderValid = [](uint32 shaderId)
		{
			int isValid = 0;
			glGetShaderiv(shaderId, GL_COMPILE_STATUS, &isValid);
			if (!isValid)
			{
				int maxLength = 0;
				glGetShaderiv(shaderId, GL_INFO_LOG_LENGTH, &maxLength);

				if (maxLength > 0)
				{
					std::vector<char> errorLog(maxLength + 1, 0);
					glGetShaderInfoLog(shaderId, maxLength, &maxLength, &errorLog[0]);
					JMESSAGE(&errorLog[0]);
				}
				glDeleteShader(shaderId);
				return false;
			}
			return true;
		};
		if (!checkShaderValid(vs))
			return nullptr;

		uint32 gs = 0;
		if (!gsText.empty())
		{
			gs = glCreateShader(GL_GEOMETRY_SHADER);
			const char* gsPtr = gsText.c_str();
			glShaderSource(gs, 1, &gsPtr, nullptr);
			glCompileShader(gs);

			if (!checkShaderValid(gs))
			{
				glDeleteShader(vs);
				return nullptr;
			}
		}

		auto fs = glCreateShader(GL_FRAGMENT_SHADER);
		const char* fsPtr = fsText.c_str();
		int32 fragment_shader_string_length = static_cast<int32>(fsText.length());
		glShaderSource(fs, 1, &fsPtr, &fragment_shader_string_length);
		glCompileShader(fs);

		if (!checkShaderValid(fs))
		{
			glDeleteShader(vs);
			if (!gs)
				glDeleteShader(gs);
			return nullptr;
		}

		program = glCreateProgram();
		glAttachShader(program, vs);
		glAttachShader(program, gs);
		glAttachShader(program, fs);
		glLinkProgram(program);

		int isValid = 0;
		glGetProgramiv(program, GL_LINK_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::vector<char> errorLog(maxLength + 1, 0);
				glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
				JMESSAGE(&errorLog[0]);
			}
			glDeleteShader(vs);
			glDeleteShader(fs);
			glDeleteShader(program);
			return nullptr;
		}

		glValidateProgram(program);
		isValid = 0;
		glGetProgramiv(program, GL_VALIDATE_STATUS, &isValid);
		if (!isValid)
		{
			int maxLength = 0;
			glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);

			if (maxLength > 0)
			{
				std::vector<char> errorLog(maxLength + 1, 0);
				glGetProgramInfoLog(program, maxLength, &maxLength, &errorLog[0]);
				JMESSAGE(&errorLog[0]);
			}
			glDeleteShader(vs);
			glDeleteShader(fs);
			glDeleteShader(program);
			return nullptr;
		}
		glDeleteShader(vs);
		glDeleteShader(fs);
	}

	auto shader = new jShader_OpenGL();
	shader->program = program;
	return shader;
}

jTexture* jRHI_OpenGL::CreateNullTexture() const
{
	auto texure = new jTexture_OpenGL();
	glGenTextures(1, &texure->TextureID);
	glBindTexture(GL_TEXTURE_2D, texure->TextureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return texure;
}

jTexture* jRHI_OpenGL::CreateTextureFromData(unsigned char* data, int32 width, int32 height)
{
	auto texure = new jTexture_OpenGL();
	glGenTextures(1, &texure->TextureID);
	glBindTexture(GL_TEXTURE_2D, texure->TextureID);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	return texure;
}

bool jRHI_OpenGL::SetUniformbuffer(const IUniformBuffer* buffer, const jShader* shader)
{
	switch (buffer->GetType())
	{
	case EUniformType::MATRIX:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformMatrix = static_cast<const jUniformBuffer<Matrix>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformMatrix->Name.c_str());
		if (loc == -1)
			return false;
		glUniformMatrix4fv(loc, 1, true, &uniformMatrix->Data.mm[0]);
		break;
	}
	case EUniformType::BOOL:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformVector = static_cast<const jUniformBuffer<bool>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformVector->Name.c_str());
		if (loc != -1)
			glUniform1i(loc, (int32)uniformVector->Data);
		break;
	}
	case EUniformType::INT:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformVector = static_cast<const jUniformBuffer<int>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformVector->Name.c_str());
		if (loc == -1)
			return false;
		glUniform1i(loc, uniformVector->Data);
		break;
	}
	case EUniformType::FLOAT:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformVector = static_cast<const jUniformBuffer<float>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformVector->Name.c_str());
		if (loc == -1)
			return false;
		glUniform1f(loc, uniformVector->Data);
		break;
	}
	case EUniformType::VECTOR2:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformVector = static_cast<const jUniformBuffer<Vector2>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformVector->Name.c_str());
		if (loc == -1)
			return false;
		glUniform2fv(loc, 1, &uniformVector->Data.v[0]);
		break;
	}
	case EUniformType::VECTOR3:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformVector = static_cast<const jUniformBuffer<Vector>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformVector->Name.c_str());
		if (loc == -1)
			return false;
		glUniform3fv(loc, 1, &uniformVector->Data.v[0]);
		break;
	}
	case EUniformType::VECTOR4:
	{
		auto shader_gl = static_cast<const jShader_OpenGL*>(shader);

		auto uniformVector = static_cast<const jUniformBuffer<Vector4>*>(buffer);
		auto loc = glGetUniformLocation(shader_gl->program, uniformVector->Name.c_str());
		if (loc == -1)
			return false;
		glUniform4fv(loc, 1, &uniformVector->Data.v[0]);
		break;
	}
	default:
		JASSERT(0);
		return false;
	}

	return true;
}

void jRHI_OpenGL::SetMatetrial(jMaterialData* materialData, const jShader* shader, int32 baseBindingIndex)
{
	int index = baseBindingIndex;
	for (int32 i = 0; i < materialData->Params.size(); ++i)
	{
		auto matParam = materialData->Params[i];
		auto tex_gl = static_cast<const jTexture_OpenGL*>(matParam->Texture);
		if (!tex_gl)
			continue;

		if (!SetUniformbuffer(&jUniformBuffer<int>(matParam->Name, index), shader))
			continue;
		SetTexture(index, matParam->Texture);

		SetTextureFilter(tex_gl->TextureType, ETextureFilterTarget::MAGNIFICATION, matParam->Magnification);
		SetTextureFilter(tex_gl->TextureType, ETextureFilterTarget::MINIFICATION, matParam->Minification);
		++index;
	}
}

void jRHI_OpenGL::SetTexture(int32 index, const jTexture* texture)
{
	glActiveTexture(GL_TEXTURE0 + index);
	
	auto texture_gl = static_cast<const jTexture_OpenGL*>(texture);

	uint32 textureType = 0;
	switch (texture_gl->TextureType)
	{
	case ETextureType::TEXTURE_2D:
		textureType = GL_TEXTURE_2D;
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
	case ETextureType::TEXTURE_2D_ARRAY_OMNISHADOW:
		textureType = GL_TEXTURE_2D;
		break;
	case ETextureType::TEXTURE_CUBE:
		textureType = GL_TEXTURE_CUBE_MAP;
		break;
	default:
		break;
	}
	
	glBindTexture(textureType, texture_gl->TextureID);
}

void jRHI_OpenGL::SetTextureFilter(ETextureType type, ETextureFilterTarget target, ETextureFilter filter)
{
	uint32 texFilter = 0;
	switch (filter)
	{
	case ETextureFilter::NEAREST:
		texFilter = GL_NEAREST;
		break;
	case ETextureFilter::LINEAR:
		texFilter = GL_LINEAR;
		break;
	default:
		break;
	}

	uint32 texTarget = 0;
	switch (target)
	{
	case ETextureFilterTarget::MINIFICATION:
		texTarget = GL_TEXTURE_MIN_FILTER;
		break;
	case ETextureFilterTarget::MAGNIFICATION:
		texTarget = GL_TEXTURE_MAG_FILTER;
		break;
	default:
		break;
	}

	uint32 textureType = 0;
	switch (type)
	{
	case ETextureType::TEXTURE_2D:
		textureType = GL_TEXTURE_2D;
		break;
	case ETextureType::TEXTURE_2D_ARRAY:
		textureType = GL_TEXTURE_2D_ARRAY;
		break;
	case ETextureType::TEXTURE_CUBE:
		textureType = GL_TEXTURE_CUBE_MAP;
		break;
	default:
		break;
	}

	glTexParameteri(textureType, texTarget, texFilter);
}

void jRHI_OpenGL::EnableCullFace(bool enable)
{
	if (enable)
		glEnable(GL_CULL_FACE);
	else
		glDisable(GL_CULL_FACE);
}

jRenderTarget* jRHI_OpenGL::CreateRenderTarget(const jRenderTargetInfo& info)
{
	uint32 internalFormat = 0;
	switch (info.InternalFormat)
	{
	case EFormat::RGB:
		internalFormat = GL_RGB;
		break;
	case EFormat::RGBA:
		internalFormat = GL_RGBA;
		break;
	case EFormat::RG:
		internalFormat = GL_RG;
		break;
	case EFormat::RG32F:
		internalFormat = GL_RG32F;
		break;
	case EFormat::RGBA32F:
		internalFormat = GL_RGBA32F;
		break;
	default:
		break;
	}

	uint32 format = 0;
	switch (info.Format)
	{
	case EFormat::RGB:
		format = GL_RGB;
		break;
	case EFormat::RGBA:
		format = GL_RGBA;
		break;
	case EFormat::RG:
		format = GL_RG;
		break;
	case EFormat::RG32F:
		format = GL_RG32F;
		break;
	case EFormat::RGBA32F:
		format = GL_RGBA32F;
		break;
	default:
		break;
	}

	uint32 formatType = 0;
	switch (info.FormatType)
	{
	case EFormatType::BYTE:
		formatType = GL_BYTE;
		break;
	case EFormatType::INT:
		formatType = GL_INT;
		break;
	case EFormatType::FLOAT:
		formatType = GL_FLOAT;
		break;
	default:
		break;
	}

	auto rt_gl = new jRenderTarget_OpenGL();
	rt_gl->Info = info;
	
	JASSERT(info.TextureCount > 0);
	if (info.TextureType == ETextureType::TEXTURE_2D)
	{
		uint32 fbo = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		rt_gl->Textures.resize(info.TextureCount);
		for (int i = 0; i < info.TextureCount; ++i)
		{
			uint32 tbo = 0;
			glGenTextures(1, &tbo);
			glBindTexture(GL_TEXTURE_2D, tbo);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, info.Width, info.Height, 0, format, formatType, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tbo, 0);
			rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);

			auto tex_gl = new jTexture_OpenGL();
			tex_gl->TextureType = info.TextureType;
			tex_gl->TextureID = tbo;
			rt_gl->Textures[i] = tex_gl;
		}

		uint32 rbo = 0;
		glGenRenderbuffers(1, &rbo);
		glBindRenderbuffer(GL_RENDERBUFFER, rbo);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, info.Width, info.Height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			char szTemp[256] = { 0, };
			sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2D framebuffer which is not complete : %d", status_code);
			JMESSAGE(szTemp);
			return nullptr;
		}

	
		rt_gl->fbos.push_back(fbo);
		rt_gl->rbos.push_back(rbo);
	}
	else if (info.TextureType == ETextureType::TEXTURE_2D_ARRAY)
	{
		auto tex_gl = new jTexture_OpenGL();

		uint32 tbo = 0;
		glGenTextures(1, &tbo);
		glBindTexture(GL_TEXTURE_2D_ARRAY, tbo);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, internalFormat, info.Width, info.Height, info.TextureCount, 0, format, formatType, nullptr);

		uint32 fbo = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		for (int32 i = 0; i < info.TextureCount; ++i)
		{
			glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, tbo, 0, i);
			rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);
		}

		uint32 rbo = 0;
		glGenRenderbuffers(1, &rbo);
		glBindRenderbuffer(GL_RENDERBUFFER, rbo);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, info.Width, info.Height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			char szTemp[256] = { 0, };
			sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2DArray framebuffer which is not complete : %d", status_code);
			JMESSAGE(szTemp);
			return nullptr;
		}

		tex_gl->TextureType = info.TextureType;
		tex_gl->TextureID = tbo;
		rt_gl->Textures.push_back(tex_gl);

		rt_gl->fbos.push_back(fbo);
		rt_gl->rbos.push_back(rbo);
	}
	else if (info.TextureType == ETextureType::TEXTURE_2D_ARRAY_OMNISHADOW)
	{
		uint32 fbo = 0;
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);

		rt_gl->Textures.resize(info.TextureCount);

		JASSERT(info.TextureCount == 1);
		for (int i = 0; i < info.TextureCount; ++i)
		{
			uint32 tbo = 0;
			glGenTextures(1, &tbo);
			glBindTexture(GL_TEXTURE_2D, tbo);
			glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, info.Width, info.Height, 0, format, formatType, nullptr);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, tbo, 0);
			rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + i);

			auto tex_gl = new jTexture_OpenGL();
			tex_gl->TextureType = info.TextureType;
			tex_gl->TextureID = tbo;
			rt_gl->Textures[i] = tex_gl;
		}

		uint32 rbo = 0;
		glGenRenderbuffers(1, &rbo);
		glBindRenderbuffer(GL_RENDERBUFFER, rbo);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, info.Width, info.Height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
			char szTemp[256] = { 0, };
			sprintf_s(szTemp, sizeof(szTemp), "Failed to create Texture2D framebuffer which is not complete : %d", status_code);
			JMESSAGE(szTemp);
			return nullptr;
		}

		rt_gl->fbos.push_back(fbo);
		rt_gl->rbos.push_back(rbo);
	}
	else if (info.TextureType == ETextureType::TEXTURE_CUBE)
	{
		uint32 tbo = 0;
		glGenTextures(1, &tbo);
		glBindTexture(GL_TEXTURE_CUBE_MAP, tbo);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


		for (int i = 0; i < 6; ++i)
			glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, info.Width, info.Height, 0, format, formatType, nullptr);

		auto tex_gl = new jTexture_OpenGL();
		tex_gl->TextureType = info.TextureType;
		tex_gl->TextureID = tbo;
		rt_gl->Textures.push_back(tex_gl);

		for (int i = 0; i < 6; ++i)
		{
			uint32 fbo = 0;
			glGenFramebuffers(1, &fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fbo);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, tbo, 0);

			uint32 rbo = 0;
			glGenRenderbuffers(1, &rbo);
			glBindRenderbuffer(GL_RENDERBUFFER, rbo);
			glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, info.Width, info.Height);
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);

			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				auto status_code = glCheckFramebufferStatus(GL_FRAMEBUFFER);
				char szTemp[256] = { 0, };
				sprintf_s(szTemp, sizeof(szTemp), "Failed to create TextureCube framebuffer %d which is not complete : %d", i, status_code);
				JMESSAGE(szTemp);
				return nullptr;
			}

			rt_gl->fbos.push_back(fbo);
			rt_gl->rbos.push_back(rbo);
		}

		rt_gl->drawBuffers.push_back(GL_COLOR_ATTACHMENT0);
	}
	else
	{
		JMESSAGE("Unsupported type texture in FramebufferPool");
		return nullptr;
	}

	return rt_gl;
}

void jRHI_OpenGL::EnableDepthTest(bool enable)
{
	if (enable)
		glEnable(GL_DEPTH_TEST);
	else
		glDisable(GL_DEPTH_TEST);
}

void jRHI_OpenGL::SetRenderTarget(const jRenderTarget* rt, int32 index /*= 0*/, bool mrt /*= false*/)
{
	if (rt)
	{
		rt->Begin(index, mrt);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
	}
}

void jRHI_OpenGL::SetDrawBuffers(const std::initializer_list<EDrawBufferType>& list)
{
	std::vector<uint32> buffers;
	for(auto& iter : list)
	{
		switch (iter)
		{
		case EDrawBufferType::COLOR_ATTACHMENT0:
			buffers.push_back(GL_COLOR_ATTACHMENT0);
			break;
		case EDrawBufferType::COLOR_ATTACHMENT1:
			buffers.push_back(GL_COLOR_ATTACHMENT1);
			break;
		case EDrawBufferType::COLOR_ATTACHMENT2:
			buffers.push_back(GL_COLOR_ATTACHMENT2);
			break;
		case EDrawBufferType::COLOR_ATTACHMENT3:
			buffers.push_back(GL_COLOR_ATTACHMENT3);
			break;
		case EDrawBufferType::COLOR_ATTACHMENT4:
			buffers.push_back(GL_COLOR_ATTACHMENT4);
			break;
		case EDrawBufferType::COLOR_ATTACHMENT5:
			buffers.push_back(GL_COLOR_ATTACHMENT5);
			break;
		case EDrawBufferType::COLOR_ATTACHMENT6:
			buffers.push_back(GL_COLOR_ATTACHMENT6);
			break;
		default:
			JASSERT(0);
			break;
		}
	}

	if (!buffers.empty())
		glDrawBuffers(static_cast<int32>(buffers.size()), &buffers[0]);
}

void jRHI_OpenGL::UpdateVertexBuffer(jVertexBuffer* vb, const std::shared_ptr<jVertexStreamData>& streamData)
{
	auto vb_gl = static_cast<jVertexBuffer_OpenGL*>(vb);

	glBindVertexArray(vb_gl->VAO);
	for (auto& iter : vb_gl->Streams)
		glDeleteBuffers(1, &iter.BufferID);
	vb_gl->Streams.clear();

	vb_gl->VertexStreamData = streamData;

	for (const auto& iter : streamData->Params)
	{
		if (iter->Stride <= 0)
			continue;

		unsigned int bufferType = GL_STATIC_DRAW;
		switch (iter->BufferType)
		{
		case EBufferType::STATIC:
			bufferType = GL_STATIC_DRAW;
			break;
		case EBufferType::DYNAMIC:
			bufferType = GL_DYNAMIC_DRAW;
			break;
		}

		jVertexStream_OpenGL stream;
		glGenBuffers(1, &stream.BufferID);
		glBindBuffer(GL_ARRAY_BUFFER, stream.BufferID);
		glBufferData(GL_ARRAY_BUFFER, iter->GetBufferSize(), iter->GetBufferData(), bufferType);
		stream.Name = iter->Name;
		stream.Count = iter->Stride / iter->ElementTypeSize;
		stream.BufferType = iter->BufferType;
		stream.ElementType = iter->ElementType;
		stream.Stride = iter->Stride;
		stream.Offset = 0;

		vb_gl->Streams.emplace_back(stream);
	}
}

void jRHI_OpenGL::UpdateVertexBuffer(jVertexBuffer* vb, IStreamParam* streamParam, int32 streamParamIndex)
{
	JASSERT(streamParam->Stride > 0);
	if (streamParam->Stride <= 0)
		return;

	auto vb_gl = static_cast<jVertexBuffer_OpenGL*>(vb);
	
	auto stream = vb_gl->Streams[streamParamIndex];

	unsigned int bufferType = GL_STATIC_DRAW;
	switch (streamParam->BufferType)
	{
	case EBufferType::STATIC:
		bufferType = GL_STATIC_DRAW;
		break;
	case EBufferType::DYNAMIC:
		bufferType = GL_DYNAMIC_DRAW;
		break;
	}

	glBindVertexArray(vb_gl->VAO);
	glBindBuffer(GL_ARRAY_BUFFER, stream.BufferID);
	glBufferData(GL_ARRAY_BUFFER, streamParam->GetBufferSize(), streamParam->GetBufferData(), bufferType);
	stream.Name = streamParam->Name;
	stream.Count = streamParam->Stride / streamParam->ElementTypeSize;
	stream.ElementType = streamParam->ElementType;
	stream.Stride = streamParam->Stride;
	stream.Offset = 0;
}

void jRHI_OpenGL::EnableBlend(bool enable)
{
	if (enable)
		glEnable(GL_BLEND);
	else
		glDisable(GL_BLEND);
}

void jRHI_OpenGL::SetBlendFunc(EBlendSrc src, EBlendDest dest)
{
	unsigned int src_gl = 0;
	switch (src)
	{
	case EBlendSrc::ZERO:
		src_gl = GL_ZERO;
		break;
	case EBlendSrc::ONE:
		src_gl = GL_ONE;
		break;
	case EBlendSrc::SRC_COLOR:
		src_gl = GL_SRC_COLOR;
		break;
	case EBlendSrc::ONE_MINUS_SRC_COLOR:
		src_gl = GL_ONE_MINUS_SRC_COLOR;
		break;
	case EBlendSrc::DST_COLOR:
		src_gl = GL_DST_COLOR;
		break;
	case EBlendSrc::ONE_MINUS_DST_COLOR:
		src_gl = GL_ONE_MINUS_DST_COLOR;
		break;
	case EBlendSrc::SRC_ALPHA:
		src_gl = GL_SRC_ALPHA;
		break;
	case EBlendSrc::ONE_MINUS_SRC_ALPHA:
		src_gl = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case EBlendSrc::DST_ALPHA:
		src_gl = GL_DST_ALPHA;
		break;
	case EBlendSrc::ONE_MINUS_DST_ALPHA:
		src_gl = GL_ONE_MINUS_DST_ALPHA;
		break;
	case EBlendSrc::CONSTANT_COLOR:
		src_gl = GL_CONSTANT_COLOR;
		break;
	case EBlendSrc::ONE_MINUS_CONSTANT_COLOR:
		src_gl = GL_ONE_MINUS_CONSTANT_COLOR;
		break;
	case EBlendSrc::CONSTANT_ALPHA:
		src_gl = GL_CONSTANT_ALPHA;
		break;
	case EBlendSrc::ONE_MINUS_CONSTANT_ALPHA:
		src_gl = GL_ONE_MINUS_CONSTANT_ALPHA;
		break;
	case EBlendSrc::SRC_ALPHA_SATURATE:
		src_gl = GL_SRC_ALPHA_SATURATE;
		break;
	default:
		JASSERT(0);
		break;
	}

	unsigned int dest_gl = 0;
	switch (dest)
	{
	case EBlendDest::ZERO:
		dest_gl = GL_ZERO;
		break;
	case EBlendDest::ONE:
		dest_gl = GL_ONE;
		break;
	case EBlendDest::SRC_COLOR:
		dest_gl = GL_SRC_COLOR;
		break;
	case EBlendDest::ONE_MINUS_SRC_COLOR:
		dest_gl = GL_ONE_MINUS_SRC_COLOR;
		break;
	case EBlendDest::DST_COLOR:
		dest_gl = GL_DST_COLOR;
		break;
	case EBlendDest::ONE_MINUS_DST_COLOR:
		dest_gl = GL_ONE_MINUS_DST_COLOR;
		break;
	case EBlendDest::SRC_ALPHA:
		dest_gl = GL_SRC_ALPHA;
		break;
	case EBlendDest::ONE_MINUS_SRC_ALPHA:
		dest_gl = GL_ONE_MINUS_SRC_ALPHA;
		break;
	case EBlendDest::DST_ALPHA:
		dest_gl = GL_DST_ALPHA;
		break;
	case EBlendDest::ONE_MINUS_DST_ALPHA:
		dest_gl = GL_ONE_MINUS_DST_ALPHA;
		break;
	case EBlendDest::CONSTANT_COLOR:
		dest_gl = GL_CONSTANT_COLOR;
		break;
	case EBlendDest::ONE_MINUS_CONSTANT_COLOR:
		dest_gl = GL_ONE_MINUS_CONSTANT_COLOR;
		break;
	case EBlendDest::CONSTANT_ALPHA:
		dest_gl = GL_CONSTANT_ALPHA;
		break;
	case EBlendDest::ONE_MINUS_CONSTANT_ALPHA:
		dest_gl = GL_ONE_MINUS_CONSTANT_ALPHA;
		break;
	default:
		JASSERT(0);
		break;
	}

	glBlendFunc(src_gl, dest_gl);
}

void jRHI_OpenGL::EnableStencil(bool enable)
{
	if (enable)
		glEnable(GL_STENCIL_TEST);
	else
		glDisable(GL_STENCIL_TEST);
}

void jRHI_OpenGL::SetStencilOpSeparate(EFace face, EStencilOp sFail, EStencilOp dpFail, EStencilOp dpPass)
{
	unsigned int face_gl = 0;
	switch (face)
	{
	case EFace::BACK:
		face_gl = GL_BACK;
		break;
	case EFace::FRONT:
		face_gl = GL_FRONT;
		break;
	case EFace::FRONT_AND_BACK:
		face_gl = GL_FRONT_AND_BACK;
		break;
	}

	auto func = [](EStencilOp op)
	{
		switch (op)
		{
		case EStencilOp::KEEP:		return GL_KEEP;
		case EStencilOp::ZERO:		return GL_ZERO;
		case EStencilOp::REPLACE:	return GL_REPLACE;
		case EStencilOp::INCR:		return GL_INCR;
		case EStencilOp::INCR_WRAP:	return GL_INCR_WRAP;
		case EStencilOp::DECR:		return GL_DECR;
		case EStencilOp::DECR_WRAP:	return GL_DECR_WRAP;
		case EStencilOp::INVERT:	return GL_INVERT;
		default:					return GL_NONE;
		}
	};

	glStencilOpSeparate(face_gl, func(sFail), func(dpFail), func(dpPass));
}

void jRHI_OpenGL::SetStencilFunc(EDepthStencilFunc func, int32 ref, uint32 mask)
{
	unsigned int func_gl = 0;
	switch(func)
	{
	case EDepthStencilFunc::NEVER:
		func_gl = GL_NEVER;
		break;
	case EDepthStencilFunc::LESS:
		func_gl = GL_LESS;
		break;
	case EDepthStencilFunc::EQUAL:
		func_gl = GL_EQUAL;
		break;
	case EDepthStencilFunc::LEQUAL:
		func_gl = GL_LEQUAL;
		break;
	case EDepthStencilFunc::GREATER:
		func_gl = GL_GREATER;
		break;
	case EDepthStencilFunc::NOTEQUAL:
		func_gl = GL_NOTEQUAL;
		break;
	case EDepthStencilFunc::GEQUAL:
		func_gl = GL_GEQUAL;
		break;
	case EDepthStencilFunc::ALWAYS:
		func_gl = GL_ALWAYS;
		break;
	}

	glStencilFunc(func_gl, ref, mask);
}

void jRHI_OpenGL::SetDepthFunc(EDepthStencilFunc func)
{
	unsigned int func_gl = 0;
	switch (func)
	{
	case EDepthStencilFunc::NEVER:
		func_gl = GL_NEVER;
		break;
	case EDepthStencilFunc::LESS:
		func_gl = GL_LESS;
		break;
	case EDepthStencilFunc::EQUAL:
		func_gl = GL_EQUAL;
		break;
	case EDepthStencilFunc::LEQUAL:
		func_gl = GL_LEQUAL;
		break;
	case EDepthStencilFunc::GREATER:
		func_gl = GL_GREATER;
		break;
	case EDepthStencilFunc::NOTEQUAL:
		func_gl = GL_NOTEQUAL;
		break;
	case EDepthStencilFunc::GEQUAL:
		func_gl = GL_GEQUAL;
		break;
	case EDepthStencilFunc::ALWAYS:
		func_gl = GL_ALWAYS;
		break;
	}

	glDepthFunc(func_gl);
}

void jRHI_OpenGL::SetDepthMask(bool enable)
{
	glDepthMask(enable);
}

void jRHI_OpenGL::SetColorMask(bool r, bool g, bool b, bool a)
{
	glColorMask(r, g, b, a);
}

IUniformBufferBlock* jRHI_OpenGL::CreateUniformBufferBlock(const char* blockname) const
{
	auto uniformBufferBlock = new jUniformBufferBlock_OpenGL(blockname);
	uniformBufferBlock->Init();
	return uniformBufferBlock;
}

//////////////////////////////////////////////////////////////////////////
void jVertexBuffer_OpenGL::Bind(const jShader* shader) const
{
	g_rhi->BindVertexBuffer(this, shader);
}

void jIndexBuffer_OpenGL::Bind(const jShader* shader) const
{
	g_rhi->BindIndexBuffer(this, shader);
}

bool jRenderTarget_OpenGL::Begin(int index, bool mrt) const
{
	if (mrt && mrt_fbo)
	{
		glBindFramebuffer(GL_FRAMEBUFFER, mrt_fbo);
		if (mrt_drawBuffers.empty())
			glDrawBuffer(GL_NONE);
		else
			glDrawBuffers(static_cast<int32>(mrt_drawBuffers.size()), &mrt_drawBuffers[0]);
	}
	else
	{
		glBindFramebuffer(GL_FRAMEBUFFER, fbos[index]);
		if (drawBuffers.empty())
			glDrawBuffer(GL_NONE);
		else
			glDrawBuffers(static_cast<int32>(drawBuffers.size()), &drawBuffers[0]);
	}

	glViewport(0, 0, Info.Width, Info.Height);
	//glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	return true;
}

void jRenderTarget_OpenGL::End() const
{
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
}

//////////////////////////////////////////////////////////////////////////
void jUniformBufferBlock_OpenGL::UpdateBufferData(const void* newData, int32 size)
{
	Size = size;
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	glBufferData(GL_UNIFORM_BUFFER, size, newData, GL_DYNAMIC_DRAW);
}

void jUniformBufferBlock_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_UNIFORM_BUFFER, UBO);
	glClearBufferData(GL_UNIFORM_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jUniformBufferBlock_OpenGL::Init()
{
	BindingPoint = GetBindPoint();
	glGenBuffers(1, &UBO);
	glBindBufferBase(GL_UNIFORM_BUFFER, BindingPoint, UBO);
}

void jUniformBufferBlock_OpenGL::Bind(const jShader* shader) const
{
	if (-1 == BindingPoint)
		return;

	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	uint32 index = glGetUniformBlockIndex(shader_gl->program, Name.c_str());
	if (-1 != index)
		glUniformBlockBinding(shader_gl->program, index, BindingPoint);
}

void jShaderStorageBufferObject_OpenGL::Init()
{
	BindingPoint = GetBindPoint();
	glGenBuffers(1, &SSBO);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BindingPoint, SSBO);
}

void jShaderStorageBufferObject_OpenGL::UpdateBufferData(void* newData, int32 size)
{
	Size = size;
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
	glBufferData(GL_SHADER_STORAGE_BUFFER, size, newData, GL_DYNAMIC_COPY);
}

void jShaderStorageBufferObject_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
	glClearBufferData(GL_SHADER_STORAGE_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jShaderStorageBufferObject_OpenGL::GetBufferData(void* newData, int32 size)
{
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, SSBO);
	void* p = glMapBuffer(GL_SHADER_STORAGE_BUFFER, GL_READ_ONLY);
	if (p)
		memcpy(newData, p, size);
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
}

void jShaderStorageBufferObject_OpenGL::Bind(const jShader* shader) const
{
	if (-1 == BindingPoint)
		return;

	auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	uint32 index = glGetProgramResourceIndex(shader_gl->program, GL_SHADER_STORAGE_BLOCK, Name.c_str());
	if (-1 != index)
		glShaderStorageBlockBinding(shader_gl->program, index, BindingPoint);
}

void jAtomicCounterBuffer_OpenGL::Init()
{
	glGenBuffers(1, &ACBO);
	glBindBufferBase(GL_ATOMIC_COUNTER_BUFFER, BindingPoint, ACBO);
}

void jAtomicCounterBuffer_OpenGL::UpdateBufferData(void* newData, int32 size)
{
	Size = size;
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	glBufferData(GL_ATOMIC_COUNTER_BUFFER, size, newData, GL_DYNAMIC_COPY);
}

void jAtomicCounterBuffer_OpenGL::GetBufferData(void* newData, int32 size)
{
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	void* p = glMapBuffer(GL_ATOMIC_COUNTER_BUFFER, GL_READ_ONLY);
	if (p)
		memcpy(newData, p, size);
	glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);
}

void jAtomicCounterBuffer_OpenGL::ClearBuffer(int32 clearValue)
{
	glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	glClearBufferData(GL_ATOMIC_COUNTER_BUFFER, GL_R32I, GL_RED_INTEGER, GL_INT, &clearValue);
}

void jAtomicCounterBuffer_OpenGL::Bind(const jShader* shader) const
{
	if (-1 == BindingPoint)
		return;

	//glBindBuffer(GL_ATOMIC_COUNTER_BUFFER, ACBO);
	//void* p = glMapBuffer(GL_ATOMIC_COUNTER_BUFFER, GL_WRITE_ONLY);
	//if (Data)
	//	memcpy(p, Data, Size);
	//glUnmapBuffer(GL_ATOMIC_COUNTER_BUFFER);

	//auto shader_gl = static_cast<const jShader_OpenGL*>(shader);
	//uint32 index = glGetUniformLocation(shader_gl->program, Name.c_str());
	//uint32 index = glGetProgramResourceIndex(shader_gl->program, GL_ATOMIC_COUNTER_BUFFER, Name.c_str());
	//if (-1 != index)
	//	glShaderStorageBlockBinding(shader_gl->program, index, BindingPoint);
}
