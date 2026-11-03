# Reuse the x64 dependencies shipped with the existing Visual Studio project.
set(engine_dir "${PROJECT_SOURCE_DIR}/jEngine")
set(external_dir "${engine_dir}/External")
target_sources(jEngine PRIVATE
  "${external_dir}/glad/glad.c"
  "${external_dir}/IMGUI/imgui_demo.cpp"
  "${external_dir}/IMGUI/imgui_impl_dx12.cpp"
  "${external_dir}/IMGUI/imgui_impl_opengl3.cpp"
  "${external_dir}/IMGUI/imgui_impl_win32.cpp")
target_include_directories(jEngine PRIVATE
  "${external_dir}/assimp/include"
  "${external_dir}/vulkan/1.3.239.0/Include"
  "${external_dir}/glslang"
  "${external_dir}/glad/include"
  "${external_dir}/ShaderConductor/Include"
  "${external_dir}/DirectX12/include"
  "${external_dir}/WinPixEventRuntime/Include"
  "${external_dir}/DirectXTex/include")
# Existing engine translation units select configuration-specific libraries with
# pragma comment(lib). Absolute search paths also work in generated build trees.
target_link_directories(jEngine PRIVATE
  "${engine_dir}"
  "${external_dir}/assimp/lib"
  "${external_dir}/vulkan/1.3.239.0/Lib"
  "${external_dir}/glslang/lib"
  "${external_dir}/ShaderConductor/Lib"
  "${external_dir}/WinPixEventRuntime/lib/x64"
  "${external_dir}/DirectXTex/lib")
target_link_libraries(jEngine PRIVATE opengl32 glfw3)
target_compile_definitions(jEngine PRIVATE
  _CONSOLE UNICODE _UNICODE "$<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>")
target_compile_options(jEngine PRIVATE /utf-8 /W3 /sdl /permissive-)
set_target_properties(jEngine PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  VS_DEBUGGER_WORKING_DIRECTORY "${engine_dir}"
  VS_DEBUGGER_COMMAND_ARGUMENTS "-vulkan")
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT jEngine)

# Match the handwritten project's PCH exceptions, including TracyClient.cpp.
file(GLOB_RECURSE no_pch CONFIGURE_DEPENDS
  "${external_dir}/EASTL/source/*.cpp"
  "${external_dir}/IMGUI/*.cpp")
set_source_files_properties(${no_pch}
  "${external_dir}/cityhash/city.cc"
  "${external_dir}/glad/glad.c"
  "${external_dir}/TinyXml2/tinyxml2.cpp"
  "${engine_dir}/Profiler/jProfilerBackend.cpp"
  "${engine_dir}/Code/External/IMGUI/ImGuizmo/ImGuizmo.cpp"
  PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Preserve Agility SDK's D3D12 subdirectory; avoid interactive xcopy prompts.
foreach(runtime_dir IN ITEMS
    "${external_dir}/assimp/lib"
    "${external_dir}/ShaderConductor/Lib/Debug"
    "${external_dir}/DirectX12/lib"
    "${external_dir}/WinPixEventRuntime/lib/x64")
  file(GLOB_RECURSE runtime_dlls CONFIGURE_DEPENDS "${runtime_dir}/*.dll")
  foreach(dll IN LISTS runtime_dlls)
    file(RELATIVE_PATH relative_dll "${runtime_dir}" "${dll}")
    get_filename_component(relative_dir "${relative_dll}" DIRECTORY)
    add_custom_command(TARGET jEngine POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:jEngine>/${relative_dir}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${dll}" "$<TARGET_FILE_DIR:jEngine>/${relative_dll}"
      VERBATIM)
  endforeach()
endforeach()
