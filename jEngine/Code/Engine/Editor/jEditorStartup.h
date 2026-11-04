#pragma once
#include <string>
class jEditor;
bool ConfigureEditorStartup(int argc, char** argv, std::string& error);
bool HasEditorStartupScene();
bool InitializeEditorStartup(jEditor& editor, std::string& error);
bool WaitForEditorImports(jEditor& editor, unsigned timeoutMilliseconds, std::string& error);
