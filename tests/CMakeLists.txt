add_executable(cwa_core_tests core_tests.cpp)
target_link_libraries(cwa_core_tests PRIVATE cwa_core)
add_test(NAME cwa_core_tests COMMAND cwa_core_tests)

add_executable(cwa_decoder_benchmark cw_decoder_benchmark.cpp)
target_link_libraries(cwa_decoder_benchmark PRIVATE cwa_core)
add_test(NAME cwa_decoder_benchmark COMMAND cwa_decoder_benchmark)

add_executable(cwa_verification_benchmark cw_verification_benchmark.cpp)
target_link_libraries(cwa_verification_benchmark PRIVATE cwa_core)
add_test(NAME cwa_verification_benchmark COMMAND cwa_verification_benchmark)

# Operator captures are private and are therefore not CI fixtures. This
# target provides the same native replay path for local before/after audits
# without making external capture files a test-suite dependency.
add_executable(cwa_capture_replay cw_capture_replay.cpp)
target_link_libraries(cwa_capture_replay PRIVATE cwa_core)
