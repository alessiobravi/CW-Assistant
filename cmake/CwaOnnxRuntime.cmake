include_guard(GLOBAL)

set(CWA_ONNXRUNTIME_VERSION "1.28.2")

function(cwa_configure_onnx_runtime)
  if(NOT CWA_ONNXRUNTIME_ROOT)
    message(FATAL_ERROR
      "CWA_ENABLE_ONNX_CHARACTER_DECODER requires CWA_ONNXRUNTIME_ROOT")
  endif()

  cmake_path(ABSOLUTE_PATH CWA_ONNXRUNTIME_ROOT
    NORMALIZE OUTPUT_VARIABLE cwa_ort_root)
  set(cwa_ort_include "${cwa_ort_root}/include")
  set(cwa_ort_license "${cwa_ort_root}/LICENSE")
  set(cwa_ort_notices "${cwa_ort_root}/ThirdPartyNotices.txt")
  set(cwa_ort_version_file "${cwa_ort_root}/VERSION_NUMBER")
  set(cwa_ort_commit_file "${cwa_ort_root}/GIT_COMMIT_ID")

  foreach(cwa_required_path IN ITEMS
      "${cwa_ort_include}/onnxruntime_c_api.h"
      "${cwa_ort_include}/onnxruntime_cxx_api.h"
      "${cwa_ort_license}"
      "${cwa_ort_notices}"
      "${cwa_ort_version_file}"
      "${cwa_ort_commit_file}")
    if(NOT EXISTS "${cwa_required_path}")
      message(FATAL_ERROR
        "The ONNX Runtime distribution is incomplete: ${cwa_required_path}")
    endif()
  endforeach()

  file(STRINGS "${cwa_ort_version_file}" cwa_ort_version LIMIT_COUNT 1)
  file(STRINGS "${cwa_ort_commit_file}" cwa_ort_commit LIMIT_COUNT 1)
  if(NOT cwa_ort_version STREQUAL CWA_ONNXRUNTIME_VERSION)
    message(FATAL_ERROR
      "CWA_ONNXRUNTIME_ROOT must contain ONNX Runtime ${CWA_ONNXRUNTIME_VERSION}")
  endif()
  if(NOT cwa_ort_commit STREQUAL
      "33ca9628233dc8f002435e868d4c2e9f82766ca1")
    message(FATAL_ERROR
      "CWA_ONNXRUNTIME_ROOT does not match the pinned runtime commit")
  endif()

  if(WIN32)
    set(cwa_ort_library "${cwa_ort_root}/lib/onnxruntime.lib")
    set(cwa_ort_runtime "${cwa_ort_root}/lib/onnxruntime.dll")
    set(cwa_ort_provider "${cwa_ort_root}/lib/onnxruntime_providers_shared.dll")
  elseif(APPLE)
    set(cwa_ort_library
      "${cwa_ort_root}/lib/libonnxruntime.${CWA_ONNXRUNTIME_VERSION}.dylib")
    set(cwa_ort_runtime "${cwa_ort_library}")
    set(cwa_ort_provider "")
  elseif(UNIX)
    set(cwa_ort_library
      "${cwa_ort_root}/lib/libonnxruntime.so.${CWA_ONNXRUNTIME_VERSION}")
    set(cwa_ort_runtime "${cwa_ort_library}")
    set(cwa_ort_provider
      "${cwa_ort_root}/lib/libonnxruntime_providers_shared.so")
  else()
    message(FATAL_ERROR "The optional ONNX decoder is unsupported on this platform")
  endif()

  foreach(cwa_required_path IN ITEMS "${cwa_ort_library}" "${cwa_ort_runtime}")
    if(NOT EXISTS "${cwa_required_path}")
      message(FATAL_ERROR
        "The ONNX Runtime distribution is incomplete: ${cwa_required_path}")
    endif()
  endforeach()

  add_library(cwa_onnxruntime SHARED IMPORTED GLOBAL)
  add_library(CWA::ONNXRuntime ALIAS cwa_onnxruntime)
  set_target_properties(cwa_onnxruntime PROPERTIES
    IMPORTED_LOCATION "${cwa_ort_runtime}"
    INTERFACE_INCLUDE_DIRECTORIES "${cwa_ort_include}")
  if(WIN32)
    set_target_properties(cwa_onnxruntime PROPERTIES
      IMPORTED_IMPLIB "${cwa_ort_library}")
  endif()

  include(GNUInstallDirs)
  if(WIN32)
    install(FILES "${cwa_ort_runtime}"
      DESTINATION "${CMAKE_INSTALL_BINDIR}")
    if(EXISTS "${cwa_ort_provider}")
      install(FILES "${cwa_ort_provider}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()
  elseif(APPLE)
    install(FILES "${cwa_ort_runtime}"
      DESTINATION "cw-assistant-desktop.app/Contents/Frameworks"
      RENAME "libonnxruntime.1.dylib")
  else()
    install(FILES "${cwa_ort_runtime}"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cw-assistant"
      RENAME "libonnxruntime.so.1")
    if(EXISTS "${cwa_ort_provider}")
      install(FILES "${cwa_ort_provider}"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cw-assistant")
    endif()
  endif()

  install(FILES "${cwa_ort_license}" "${cwa_ort_notices}"
    DESTINATION
      "${CMAKE_INSTALL_DATAROOTDIR}/doc/cw-assistant/third-party/onnxruntime")

  set(CWA_ONNXRUNTIME_ROOT "${cwa_ort_root}" PARENT_SCOPE)
endfunction()

function(cwa_target_link_onnx_runtime target_name)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR
      "Cannot enable ONNX Runtime for missing target: ${target_name}")
  endif()
  if(NOT TARGET CWA::ONNXRuntime)
    message(FATAL_ERROR
      "CWA::ONNXRuntime is unavailable; enable the ONNX character decoder")
  endif()

  target_link_libraries("${target_name}" PRIVATE CWA::ONNXRuntime)
  if(WIN32)
    # CTest runs build-tree executables before the install/staging pass. Keep
    # the runtime beside every linked executable so the Windows loader can
    # start those tests as well as the desktop smoke target.
    add_custom_command(TARGET "${target_name}" POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:cwa_onnxruntime>"
        "$<TARGET_FILE_DIR:${target_name}>")
    set(cwa_ort_provider_runtime
      "${CWA_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll")
    if(EXISTS "${cwa_ort_provider_runtime}")
      add_custom_command(TARGET "${target_name}" POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${cwa_ort_provider_runtime}"
          "$<TARGET_FILE_DIR:${target_name}>")
    endif()
  elseif(APPLE)
    set_property(TARGET "${target_name}" APPEND PROPERTY
      INSTALL_RPATH "@executable_path/../Frameworks")
  elseif(UNIX)
    set_property(TARGET "${target_name}" APPEND PROPERTY
      INSTALL_RPATH "$ORIGIN/../lib/cw-assistant")
  endif()
endfunction()
