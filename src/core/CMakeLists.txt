add_library(cwa_core
  src/adif.cpp
  src/channel_scheduler.cpp
  src/transmit_guard.cpp
)

target_include_directories(cwa_core PUBLIC include)
target_compile_features(cwa_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(cwa_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(cwa_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
