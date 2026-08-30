cmake_minimum_required(VERSION 3.24)

project(CWAssistant VERSION 0.1.0 LANGUAGES CXX)

option(CWA_BUILD_APP "Build the command-line development host" ON)
option(CWA_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(src/core)

if(CWA_BUILD_APP)
  add_subdirectory(src/app)
endif()

if(CWA_BUILD_TESTS)
  include(CTest)
  add_subdirectory(tests)
endif()
