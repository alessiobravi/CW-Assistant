cmake_minimum_required(VERSION 3.24)

project(CWAssistant VERSION 0.1.0 LANGUAGES CXX
  HOMEPAGE_URL "https://github.com/alessiobravi/CW-Assistant")

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
  set(CPACK_PACKAGE_VENDOR "CW Assistant contributors")
  set(CPACK_PACKAGE_CONTACT "Alessio Bravi <alessio@bravi.org>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Cross-platform multichannel amateur-radio CW assistant")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Alessio Bravi <alessio@bravi.org>")
  set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  include(CPack)
endif()
