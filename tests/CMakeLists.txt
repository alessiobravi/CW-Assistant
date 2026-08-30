add_executable(cwa_core_tests core_tests.cpp)
target_link_libraries(cwa_core_tests PRIVATE cwa_core)
add_test(NAME cwa_core_tests COMMAND cwa_core_tests)
