#pragma once

#include <filesystem>
#include <functional>

int RunViewportToolbarGizmoGpuTests(const std::filesystem::path& output,
    const std::function<void()>& frame);
