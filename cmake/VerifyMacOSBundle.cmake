if(NOT DEFINED CWA_APP_BUNDLE OR CWA_APP_BUNDLE STREQUAL "")
  message(FATAL_ERROR "CWA_APP_BUNDLE is required")
endif()
if(NOT DEFINED CWA_EXPECTED_VERSION OR CWA_EXPECTED_VERSION STREQUAL "")
  message(FATAL_ERROR "CWA_EXPECTED_VERSION is required")
endif()

set(cwa_plist "${CWA_APP_BUNDLE}/Contents/Info.plist")
if(NOT EXISTS "${cwa_plist}")
  message(FATAL_ERROR "Missing macOS bundle metadata: ${cwa_plist}")
endif()

function(cwa_require_plist_value key expected)
  execute_process(
    COMMAND /usr/libexec/PlistBuddy -c "Print :${key}" "${cwa_plist}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot read ${key}: ${error}")
  endif()
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR
      "Invalid ${key}: expected '${expected}', found '${actual}'")
  endif()
endfunction()

cwa_require_plist_value(CFBundleDisplayName "CW Assistant")
cwa_require_plist_value(CFBundleExecutable "cw-assistant-desktop")
cwa_require_plist_value(CFBundleIconFile "cw-assistant.icns")
cwa_require_plist_value(CFBundleIdentifier "it.iu0lfq.CWAssistant")
cwa_require_plist_value(CFBundleName "CW Assistant")
cwa_require_plist_value(CFBundleShortVersionString "${CWA_EXPECTED_VERSION}")
cwa_require_plist_value(CFBundleVersion "${CWA_EXPECTED_VERSION}")

if(NOT EXISTS
    "${CWA_APP_BUNDLE}/Contents/MacOS/cw-assistant-desktop")
  message(FATAL_ERROR "The declared macOS bundle executable is missing")
endif()
if(NOT EXISTS
    "${CWA_APP_BUNDLE}/Contents/Resources/cw-assistant.icns")
  message(FATAL_ERROR "The declared macOS bundle icon is missing")
endif()

execute_process(
  COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2
    "${CWA_APP_BUNDLE}"
  RESULT_VARIABLE codesign_result
  OUTPUT_VARIABLE codesign_output
  ERROR_VARIABLE codesign_error)
if(NOT codesign_result EQUAL 0)
  message(FATAL_ERROR
    "The staged macOS bundle has an invalid resource seal:\n"
    "${codesign_output}${codesign_error}")
endif()

message(STATUS "Verified macOS bundle metadata and resource seal")
