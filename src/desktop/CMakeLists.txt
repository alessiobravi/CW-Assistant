find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(cw-assistant-desktop
  main.cpp
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
)

if(WIN32)
  target_link_libraries(cw-assistant-desktop PRIVATE ole32 oleaut32)
endif()

if(MSVC)
  target_compile_options(cw-assistant-desktop PRIVATE /W4 /permissive-)
else()
  target_compile_options(cw-assistant-desktop PRIVATE -Wall -Wextra -Wpedantic)
endif()
