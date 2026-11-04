cmake_minimum_required(VERSION 3.24)
project(jEngine LANGUAGES C CXX)
if(NOT APPLE AND NOT WIN32)
  message(FATAL_ERROR "Supported platforms: Windows x64 and macOS Apple Silicon.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  find_package(Vulkan REQUIRED)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(assimp CONFIG REQUIRED)
  find_library(MOLTENVK_LIBRARY MoltenVK HINTS /opt/homebrew/lib /usr/local/lib REQUIRED)
elseif(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Windows builds require the MSVC x64 toolchain.")
endif()
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS jEngine/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/External/|MathBench\\.cpp$")
if(APPLE)
  list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/RHI/DX12/|/Shader/Spirv/|/FileLoader/jImageFileLoader\\.cpp$")
endif()
file(GLOB EASTL_SOURCES jEngine/External/EASTL/source/*.cpp)
add_executable(jEngine ${ENGINE_SOURCES} ${EASTL_SOURCES}
  jEngine/Code/External/IMGUI/ImGuizmo/ImGuizmo.cpp jEngine/External/IMGUI/imgui.cpp jEngine/External/IMGUI/imgui_draw.cpp
  jEngine/External/IMGUI/imgui_tables.cpp jEngine/External/IMGUI/imgui_widgets.cpp
  jEngine/External/IMGUI/imgui_impl_glfw.cpp jEngine/External/IMGUI/imgui_impl_vulkan.cpp
  jEngine/External/TinyXml2/tinyxml2.cpp jEngine/External/loadpng/loadpng.cpp
  jEngine/External/cityhash/city.cc
  jEngine/External/xxHash/xxhash.c
  jEngine/External/PathTracingDataLoader/GLTFLoader.cpp
  jEngine/External/PathTracingDataLoader/jPathTracingData.cpp
  jEngine/External/PathTracingDataLoader/PathTracingDataLoader.cpp)
target_include_directories(jEngine PRIVATE jEngine/External/spdlog/include jEngine jEngine/External
  jEngine/External/IMGUI jEngine/External/EASTL/include jEngine/External/EABase/include/Common
  jEngine/External/stb jEngine/External/loadpng jEngine/External/TinyXml2
  jEngine/External/xxHash jEngine/External/tracy/public)
find_program(JENGINE_DXC_EXECUTABLE NAMES dxc HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.ci-tools/dxc/bin/x64"
    "${CMAKE_CURRENT_SOURCE_DIR}/../DirectXShaderCompiler/build/macos/bin" REQUIRED)
if(WIN32)
  include(cmake/WindowsDependencies.cmake)
else()
  target_sources(jEngine PRIVATE platform/macos/jImageFileLoader.cpp)
  target_link_libraries(jEngine PRIVATE Vulkan::Vulkan glfw assimp::assimp)
  target_compile_definitions(jEngine PRIVATE JPROFILE_BACKEND=0 USE_SSE_MATRIX=0 VK_ENABLE_BETA_EXTENSIONS)
  target_compile_options(jEngine PRIVATE -fms-extensions -Wno-deprecated-declarations -Wno-invalid-constexpr -Wno-switch -Wno-unused-value -ferror-limit=10)
endif()
target_precompile_headers(jEngine PRIVATE jEngine/pch.h)

file(GLOB RECAST_SOURCES jEngine/External/recastnavigation/Recast/Source/*.cpp jEngine/External/recastnavigation/Detour/Source/*.cpp)
target_sources(jEngine PRIVATE ${RECAST_SOURCES})
target_include_directories(jEngine PRIVATE jEngine/External/recastnavigation/Recast/Include jEngine/External/recastnavigation/Detour/Include)
set_source_files_properties(${RECAST_SOURCES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_compile_definitions(jEngine PRIVATE JENGINE_DXC_EXECUTABLE="${JENGINE_DXC_EXECUTABLE}")
set_source_files_properties(jEngine/External/xxHash/xxhash.c jEngine/Tests/jEffectsAuditTests.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

include(CTest)
if(BUILD_TESTING)
  set(JENGINE_BUILD_PLUGIN_FIXTURES ON CACHE BOOL "Build editor plugin ABI fixtures" FORCE)
  add_subdirectory(examples/editor-plugin)
  add_dependencies(jEngine jengine_plugin_fixture_v1 jengine_plugin_fixture_v2 jengine_plugin_fixture_badabi jengine_plugin_fixture_failload jengine_plugin_fixture_failrestore jengine_plugin_fixture_oversized jengine_plugin_fixture_missingexport)
  target_compile_definitions(jEngine PRIVATE JENGINE_PLUGIN_FIXTURE_DIRECTORY="$<TARGET_FILE_DIR:jengine_plugin_fixture_v1>")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME cpu-math COMMAND jEngine --ci-cpu)
  add_test(NAME cli-input
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ci/check-cli.py"
      --engine "$<TARGET_FILE:jEngine>" --source "${CMAKE_CURRENT_SOURCE_DIR}/jEngine")
  set_tests_properties(cli-input PROPERTIES LABELS "cpu;io" TIMEOUT 240)
  add_test(NAME shader-contracts
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ci/check-shaders.py"
      --engine "$<TARGET_FILE:jEngine>" --dxc "${JENGINE_DXC_EXECUTABLE}"
      --source "${CMAKE_CURRENT_SOURCE_DIR}/jEngine"
      --output "${CMAKE_BINARY_DIR}/shader-checks")
  set_tests_properties(cpu-math shader-contracts PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/jEngine" TIMEOUT 180)
  if(APPLE)
    add_test(NAME vulkan-sync-contracts
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ci/check-vulkan-sync.py"
        --compiler "${CMAKE_CXX_COMPILER}")
    set_tests_properties(vulkan-sync-contracts PROPERTIES LABELS "cpu;rhi" TIMEOUT 60)
  endif()
  set_tests_properties(cpu-math PROPERTIES LABELS "cpu")
  set_tests_properties(shader-contracts PROPERTIES LABELS "cpu;shader")
endif()

target_link_libraries(jEngine PRIVATE ${CMAKE_DL_LIBS})
