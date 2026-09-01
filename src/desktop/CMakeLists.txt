find_package(Qt6 6.5 REQUIRED COMPONENTS
  Core Gui Multimedia Qml Quick QuickControls2 QuickDialogs2 SerialPort WebSockets)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(cw-assistant-desktop
  main.cpp
  replay/replay_controller.cpp
  replay/replay_controller.hpp
  replay/decoder_channel_model.cpp
  replay/decoder_channel_model.hpp
  replay/live_audio_worker.cpp
  replay/live_audio_worker.hpp
  radio/cat4om_client.cpp
  radio/cat4om_client.hpp
  settings/app_settings.cpp
  settings/app_settings.hpp
  visualization/spectrum_frame.hpp
  visualization/spectrum_waterfall_item.cpp
  visualization/spectrum_waterfall_item.hpp
)

set_target_properties(cw-assistant-desktop PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "CW Assistant"
  MACOSX_BUNDLE_GUI_IDENTIFIER "it.iu0lfq.CWAssistant"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_ICON_FILE "cw-assistant.icns"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
  WIN32_EXECUTABLE TRUE
)

set_source_files_properties(packaging/cw-assistant.png PROPERTIES
  QT_RESOURCE_ALIAS cw-assistant.png)
qt_add_resources(cw-assistant-desktop "cwa_application_icons"
  PREFIX "/icons"
  FILES packaging/cw-assistant.png)

if(WIN32)
  target_sources(cw-assistant-desktop PRIVATE
    packaging/cw-assistant.rc
    packaging/cw-assistant.ico)
endif()

if(APPLE)
  set_source_files_properties(packaging/cw-assistant.icns PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  target_sources(cw-assistant-desktop PRIVATE packaging/cw-assistant.icns)
endif()

set_source_files_properties(qml/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
set_source_files_properties(qml/ProfileChooser.qml PROPERTIES QT_RESOURCE_ALIAS ProfileChooser.qml)
set_source_files_properties(qml/SetupWizard.qml PROPERTIES QT_RESOURCE_ALIAS SetupWizard.qml)
set_source_files_properties(qml/SettingsPane.qml PROPERTIES QT_RESOURCE_ALIAS SettingsPane.qml)

qt_add_qml_module(cw-assistant-desktop
  URI CWAssistant
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/ProfileChooser.qml
    qml/SetupWizard.qml
    qml/SettingsPane.qml
)

target_link_libraries(cw-assistant-desktop PRIVATE
  cwa_core
  Qt6::Core
  Qt6::Gui
  Qt6::Multimedia
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::QuickDialogs2
  Qt6::SerialPort
  Qt6::WebSockets
)
target_compile_definitions(cw-assistant-desktop PRIVATE
  CWA_VERSION="${PROJECT_VERSION}")

if(CWA_BUILD_TESTS)
  qt_add_executable(cwa_live_audio_pipeline_test
    ${PROJECT_SOURCE_DIR}/tests/desktop/live_audio_pipeline_test.cpp
    replay/live_audio_worker.cpp
    replay/live_audio_worker.hpp
    replay/decoder_channel_model.cpp
    replay/decoder_channel_model.hpp
  )
  target_link_libraries(cwa_live_audio_pipeline_test PRIVATE
    cwa_core Qt6::Core Qt6::Multimedia)
  target_include_directories(cwa_live_audio_pipeline_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME cwa_live_audio_pipeline_test
    COMMAND cwa_live_audio_pipeline_test)
  set_tests_properties(cwa_live_audio_pipeline_test PROPERTIES TIMEOUT 5)

  qt_add_executable(cwa_desktop_render_startup_test
    ${PROJECT_SOURCE_DIR}/tests/desktop/spectrum_waterfall_startup_test.cpp
    replay/replay_controller.cpp
    replay/replay_controller.hpp
    replay/live_audio_worker.cpp
    replay/live_audio_worker.hpp
    replay/decoder_channel_model.cpp
    replay/decoder_channel_model.hpp
    visualization/spectrum_frame.hpp
    visualization/spectrum_waterfall_item.cpp
    visualization/spectrum_waterfall_item.hpp
  )
  target_link_libraries(cwa_desktop_render_startup_test PRIVATE
    cwa_core Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Quick)
  target_include_directories(cwa_desktop_render_startup_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME cwa_desktop_render_startup_test
    COMMAND cwa_desktop_render_startup_test)
  set_tests_properties(cwa_desktop_render_startup_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 10)

  add_test(NAME cwa_desktop_qml_startup_smoke
    COMMAND cw-assistant-desktop --profile ci-smoke --smoke-test)
  set_tests_properties(cwa_desktop_qml_startup_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 15)
endif()

include(GNUInstallDirs)
install(TARGETS cw-assistant-desktop
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_qml_app_script(
  TARGET cw-assistant-desktop
  OUTPUT_SCRIPT cwa_deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${cwa_deploy_script})

if(UNIX AND NOT APPLE)
  install(FILES packaging/cw-assistant.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
  install(FILES packaging/cw-assistant.png
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps)
  install(FILES packaging/it.iu0lfq.CWAssistant.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
endif()

install(FILES ${PROJECT_SOURCE_DIR}/LICENSE
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/cw-assistant
  RENAME copyright)
install(FILES ${PROJECT_SOURCE_DIR}/docs/licensing.md
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/cw-assistant)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/docs/manuals/
  DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/cw-assistant/manuals
  FILES_MATCHING PATTERN "*.md")

# Hosted builds deploy Qt shared libraries, so preserve the corresponding Qt
# license texts beside the application documentation when the SDK provides them.
get_filename_component(cwa_qt_prefix "${Qt6Core_DIR}/../../.." ABSOLUTE)
if(EXISTS "${cwa_qt_prefix}/LICENSES")
  install(DIRECTORY "${cwa_qt_prefix}/LICENSES/"
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/cw-assistant/third-party/qt6)
endif()

if(WIN32)
  target_link_libraries(cw-assistant-desktop PRIVATE ole32 oleaut32)
endif()

if(MSVC)
  target_compile_options(cw-assistant-desktop PRIVATE /W4 /permissive-)
else()
  target_compile_options(cw-assistant-desktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
