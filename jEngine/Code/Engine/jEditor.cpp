#include "pch.h"
#ifdef ENABLE_EDITOR_FEATURES
#include "jEditor.h"
#include "Editor/jEditorRuntime.h"

jEditor* g_Editor = nullptr;
jEditor::jEditor() : Runtime(std::make_unique<jEditorRuntime>(*this)) {}
jEditor::~jEditor() { ShutdownWorld(); }
#endif
