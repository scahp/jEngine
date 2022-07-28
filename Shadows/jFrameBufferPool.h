#pragma once

class jFrameBufferPool
{
public:
	jFrameBufferPool();
	~jFrameBufferPool();

	static std::shared_ptr<jFrameBuffer> GetFrameBuffer(const jFrameBufferInfo& info);
	static void ReturnFrameBuffer(jFrameBuffer* renderTarget);

	struct jFrameBufferPoolResource
	{
		bool IsUsing = false;
		std::shared_ptr<jFrameBuffer> FrameBufferPtr;
	};
	static std::map<size_t, std::list<jFrameBufferPoolResource> > FrameBufferResourceMap;
	static std::map<jFrameBuffer*, size_t> FrameBufferHashVariableMap;

	static struct jTexture* GetNullTexture(ETextureType type);
};

