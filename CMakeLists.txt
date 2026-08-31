cmake_minimum_required(VERSION 3.24)

if(APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING
    "Minimum supported macOS version (Sonoma or newer)")
endif()

project(CWAssistant VERSION 0.1.0 LANGUAGES CXX
  HOMEPAGE_URL "https://github.com/alessiobravi/CW-Assistant")

set(CWA_PACKAGE_REVISION "0" CACHE STRING
  "Numeric hosted-build revision used by upgrade-capable packages")

option(CWA_BUILD_APP "Build the command-line development host" ON)
option(CWA_BUILD_DESKTOP "Build the Qt Quick desktop application" OFF)
option(CWA_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/core)

if(CWA_BUILD_APP)
  add_subdirectory(src/app)
endif()

if(CWA_BUILD_DESKTOP)
  add_subdirectory(src/desktop)
endif()

if(CWA_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()

if(CWA_BUILD_DESKTOP)
  set(CPACK_PACKAGE_NAME "cw-assistant")
  set(CPACK_PACKAGE_VENDOR "Alessio Bravi (IU0LFQ / AD2FC)")
  set(CPACK_PACKAGE_CONTACT "Alessio Bravi <alessio@bravi.org>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Cross-platform multichannel amateur-radio CW assistant")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://iu0lfq.it/")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "CW Assistant")
  set(CPACK_PACKAGE_EXECUTABLES
      "cw-assistant-desktop" "CW Assistant")
  set(CPACK_CREATE_DESKTOP_LINKS "cw-assistant-desktop")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Alessio Bravi <alessio@bravi.org>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  if(WIN32)
    # CPack's WiX generator accepts UTF-8 .txt or RTF license input. Keep the
    # canonical GPL text extensionless in the repository and copy it at build
    # time so every installer presents the exact same license.
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
      "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt" COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_BINARY_DIR}/LICENSE.txt")
    if(NOT CWA_PACKAGE_REVISION MATCHES "^[0-9]+$")
      message(FATAL_ERROR "CWA_PACKAGE_REVISION must contain only digits")
    endif()
    if(CWA_PACKAGE_REVISION GREATER 65535)
      message(FATAL_ERROR "CWA_PACKAGE_REVISION exceeds the MSI build limit")
    endif()
    set(CPACK_PACKAGE_VERSION
        "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${CWA_PACKAGE_REVISION}")
    set(CPACK_WIX_VERSION 3)
    set(CPACK_WIX_ARCHITECTURE x64)
    set(CPACK_WIX_INSTALL_SCOPE perMachine)
    set(CPACK_WIX_UPGRADE_GUID "3310FD2E-106F-4A39-A031-A602683BCB19")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "CW Assistant")
    set(CPACK_WIX_PROPERTY_ARPURLINFOABOUT "https://iu0lfq.it/")
    set(CPACK_WIX_PROPERTY_ARPURLUPDATEINFO
        "https://github.com/alessiobravi/CW-Assistant/releases/tag/continuous")
  endif()
  include(CPack)
endif()
