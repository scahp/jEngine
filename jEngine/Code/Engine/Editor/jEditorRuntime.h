#pragma once
#ifdef ENABLE_EDITOR_FEATURES
#include <memory>
class jEditor;
// Implementation boundary: the document owns plain authored data; this owner-
// thread adapter owns instances, shared prototypes and import completions.
class jEditorRuntime
{
public:
    struct Impl;
    explicit jEditorRuntime(jEditor& editor);
    ~jEditorRuntime();
    std::unique_ptr<Impl> Data;
};
#endif
