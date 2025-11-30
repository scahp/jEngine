#pragma once

// Editor-specific features that can be excluded from runtime builds
// Define ENABLE_EDITOR_FEATURES to include editor functionality

#ifdef ENABLE_EDITOR_FEATURES

#include "PlacementTool/jPlacementTool.h"

class jEditor
{
public:
	jEditor();
	~jEditor();

	// Object/Light Placement Tool
	jPlacementTool Placement;

	// Add more editor features here in the future
	// e.g., TerrainEditor, MaterialEditor, etc.
};

extern jEditor* g_Editor;

#endif // ENABLE_EDITOR_FEATURES
