# Prepare the managed compiler before find_program and before runtime DLL copies.
# An explicit different compiler path stays under the caller's control.
option(JENGINE_AUTO_SETUP_DXC "Automatically prepare the pinned Windows DXC package" ON)
set(JENGINE_DXC_BOOTSTRAP_COMMAND)
set(_jengine_managed_dxc "${PROJECT_SOURCE_DIR}/.ci-tools/dxc/bin/x64/dxc.exe")
set(_jengine_use_managed_dxc FALSE)
if(NOT JENGINE_DXC_EXECUTABLE OR JENGINE_DXC_EXECUTABLE MATCHES "-NOTFOUND$")
  set(_jengine_use_managed_dxc TRUE)
else()
  get_filename_component(_jengine_selected_dxc "${JENGINE_DXC_EXECUTABLE}" ABSOLUTE BASE_DIR "${PROJECT_SOURCE_DIR}")
  string(TOLOWER "${_jengine_selected_dxc}" _jengine_selected_dxc)
  string(TOLOWER "${_jengine_managed_dxc}" _jengine_expected_dxc)
  if(_jengine_selected_dxc STREQUAL _jengine_expected_dxc)
    set(_jengine_use_managed_dxc TRUE)
  endif()
endif()
if(JENGINE_AUTO_SETUP_DXC AND _jengine_use_managed_dxc)
  find_program(JENGINE_POWERSHELL_EXECUTABLE NAMES powershell.exe pwsh.exe REQUIRED)
  mark_as_advanced(JENGINE_POWERSHELL_EXECUTABLE)
  set(JENGINE_DXC_BOOTSTRAP_COMMAND
    "${JENGINE_POWERSHELL_EXECUTABLE}" -NoProfile -NonInteractive -ExecutionPolicy Bypass
    -File "${PROJECT_SOURCE_DIR}/scripts/ci/setup-windows-dxc.ps1")
  message(STATUS "Checking pinned DXC; first use downloads the verified package")
  execute_process(COMMAND ${JENGINE_DXC_BOOTSTRAP_COMMAND}
    RESULT_VARIABLE _jengine_dxc_result
    OUTPUT_VARIABLE _jengine_dxc_output ERROR_VARIABLE _jengine_dxc_error
    TIMEOUT 600)
  if(NOT "${_jengine_dxc_result}" STREQUAL "0")
    message(FATAL_ERROR "Automatic DXC setup failed (${_jengine_dxc_result}):\n${_jengine_dxc_output}${_jengine_dxc_error}")
  endif()
  set(JENGINE_DXC_EXECUTABLE "${_jengine_managed_dxc}" CACHE FILEPATH "Selected DXC compiler" FORCE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/scripts/ci/dependencies.json"
    "${PROJECT_SOURCE_DIR}/scripts/ci/setup-windows-dxc.ps1")
endif()
