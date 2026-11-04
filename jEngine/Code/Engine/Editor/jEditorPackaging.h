#pragma once

#include "FileLoader/jEditorAssets.h"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace jEditorPackaging
{
using FinalizeFunction = std::function<bool(const std::filesystem::path& stagedRoot,
    std::string& error)>;

// resourceRoot is the engine's Resource directory, not a project/asset directory.
// Appends only explicit runtime inputs. No destination files are modified here.
// Retain finalize until Build finishes: it also owns generated loader/launcher inputs.
// Invoke finalize on a fresh staged package BEFORE computing manifest fingerprints.
// Missing tools, unresolved libraries or destination collisions fail with an error.
bool PrepareRuntime(const std::filesystem::path& binaryPath,
    const std::filesystem::path& resourceRoot,
    std::vector<jEditorRuntimeFile>& files,
    std::string& runtimeExecutable,
    FinalizeFunction& finalize,
    std::string& error);
}
