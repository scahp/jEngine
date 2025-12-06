#pragma once
#include <limits>

// Object and RenderObject ID types
using jObjectID = uint32;
using jRenderObjectID = uint32;

namespace jObjectIDConstants
{
	static constexpr jObjectID Invalid = std::numeric_limits<jObjectID>::max();
}

namespace jRenderObjectIDConstants
{
	static constexpr jRenderObjectID Invalid = std::numeric_limits<jRenderObjectID>::max();
}
