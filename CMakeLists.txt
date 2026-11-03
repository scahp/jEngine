cmake_minimum_required(VERSION 3.24)
project(jEngine LANGUAGES C CXX)
if(NOT APPLE)
  message(FATAL_ERROR "This CMake target is the macOS Vulkan port; use jEngine.sln for Windows.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_library(MOLTENVK_LIBRARY MoltenVK HINTS /opt/homebrew/lib /usr/local/lib REQUIRED)
file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS jEngine/*.cpp)
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/(External|RHI/DX12)/|MathBench\\.cpp$|Shader/Spirv/")
list(FILTER ENGINE_SOURCES EXCLUDE REGEX "/FileLoader/jImageFileLoader\\.cpp$")
file(GLOB EASTL_SOURCES jEngine/External/EASTL/source/*.cpp)
add_executable(jEngine ${ENGINE_SOURCES} ${EASTL_SOURCES}
  platform/macos/jImageFileLoader.cpp
  jEngine/Code/External/IMGUI/ImGuizmo/ImGuizmo.cpp jEngine/External/IMGUI/imgui.cpp jEngine/External/IMGUI/imgui_draw.cpp
  jEngine/External/IMGUI/imgui_tables.cpp jEngine/External/IMGUI/imgui_widgets.cpp
  jEngine/External/IMGUI/imgui_impl_glfw.cpp jEngine/External/IMGUI/imgui_impl_vulkan.cpp
  jEngine/External/TinyXml2/tinyxml2.cpp jEngine/External/loadpng/loadpng.cpp
  jEngine/External/cityhash/city.cc
  jEngine/External/xxHash/xxhash.c
  jEngine/External/PathTracingDataLoader/GLTFLoader.cpp
  jEngine/External/PathTracingDataLoader/jPathTracingData.cpp
  jEngine/External/PathTracingDataLoader/PathTracingDataLoader.cpp)
target_include_directories(jEngine PRIVATE jEngine jEngine/External
  jEngine/External/IMGUI jEngine/External/EASTL/include jEngine/External/EABase/include/Common
  jEngine/External/stb jEngine/External/loadpng jEngine/External/TinyXml2
  jEngine/External/xxHash jEngine/External/tracy/public)
target_link_libraries(jEngine PRIVATE Vulkan::Vulkan glfw assimp::assimp)
target_compile_definitions(jEngine PRIVATE JPROFILE_BACKEND=0 USE_SSE_MATRIX=0 VK_ENABLE_BETA_EXTENSIONS)
target_compile_options(jEngine PRIVATE -fms-extensions -Wno-deprecated-declarations -Wno-invalid-constexpr -Wno-switch -Wno-unused-value -ferror-limit=10)
target_precompile_headers(jEngine PRIVATE jEngine/pch.h)
find_program(JENGINE_DXC_EXECUTABLE NAMES dxc HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../DirectXShaderCompiler/build/macos/bin" REQUIRED)
target_compile_definitions(jEngine PRIVATE JENGINE_DXC_EXECUTABLE="${JENGINE_DXC_EXECUTABLE}")
set_source_files_properties(jEngine/External/xxHash/xxhash.c PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME cpu-math COMMAND jEngine --ci-cpu)
  add_test(NAME shader-contracts
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/ci/check-shaders.py"
      --engine "$<TARGET_FILE:jEngine>" --dxc "${JENGINE_DXC_EXECUTABLE}"
      --source "${CMAKE_CURRENT_SOURCE_DIR}/jEngine"
      --output "${CMAKE_BINARY_DIR}/shader-checks")
  set_tests_properties(cpu-math shader-contracts PROPERTIES
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/jEngine" TIMEOUT 180)
  set_tests_properties(cpu-math PROPERTIES LABELS "cpu")
  set_tests_properties(shader-contracts PROPERTIES LABELS "cpu;shader")
endif()
