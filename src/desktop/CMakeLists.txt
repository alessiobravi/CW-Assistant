find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort WebSockets)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(cw-assistant-desktop
  main.cpp
  radio/cat4om_client.cpp
  radio/cat4om_client.hpp
  settings/app_settings.cpp
  settings/app_settings.hpp
)

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
  Qt6::Qml
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::SerialPort
  Qt6::WebSockets
)

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
  install(FILES packaging/cw-assistant.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
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
