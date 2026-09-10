add_library(cwa_core
  src/adif.cpp
  src/callsign_evidence.cpp
  src/callsign_policy.cpp
  src/cat4om_protocol.cpp
  src/channel_scheduler.cpp
  src/cw_acoustic_refinement.cpp
  src/cw_character_decoder.cpp
  src/cw_character_lane_frontend.cpp
  src/cw_channel_bank.cpp
  src/cw_context_rescorer.cpp
  src/cw_vocabulary.cpp
  src/cw_decoder.cpp
  src/cw_event_lattice.cpp
  src/cw_probabilistic_decoder.cpp
  src/cw_semi_markov_segmenter.cpp
  src/cw_turn_timing_fingerprint.cpp
  src/cw_transmit_encoder.cpp
  src/cw_transmit_scheduler.cpp
  src/conversation_profile.cpp
  src/frequency_plan.cpp
  src/hardware_acceptance.cpp
  src/iq_receive.cpp
  src/iq_writer.cpp
  src/offline_callsign_database.cpp
  src/reference_rig_profiles.cpp
  src/radio_control.cpp
  src/remote_control.cpp
  src/station_equipment.cpp
  src/spectrum_analyzer.cpp
  src/transmit_guard.cpp
  src/wav_replay_source.cpp
  src/wav_writer.cpp
)

target_include_directories(cwa_core PUBLIC include)
target_compile_features(cwa_core PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(cwa_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(cwa_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
