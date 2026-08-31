import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    signal done()
    signal setupRequested()
    padding: 0
    background: Rectangle { color: "#151b23" }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Label { text: "Settings"; font.pixelSize: 22; font.weight: Font.DemiBold }
            Label { text: "Profile: " + appSettings.profileName; color: "#8290a0" }
            Item { Layout.fillWidth: true }
            ToolButton { text: "Close"; onClicked: root.done() }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Radio" }
            TabButton { text: "Keying" }
            TabButton { text: "Display" }
            TabButton { text: "Station" }
            TabButton { text: "About" }
        }

        StackLayout {
            currentIndex: tabs.currentIndex
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                contentWidth: availableWidth
                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 18
                    rowSpacing: 12
                    anchors.margins: 22
                    Label { text: "Reference radio" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: appSettings.referenceRigNames
                        currentIndex: appSettings.referenceRigIndex
                        onActivated: appSettings.selectReferenceRig(currentIndex)
                    }
                    Label { text: "Frequency control" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["OmniRig (Windows)", "Hamlib", "CAT4OM network service"]
                        currentIndex: appSettings.frequencyBackendIndex
                        onActivated: appSettings.frequencyBackendIndex = currentIndex
                    }
                    Label { text: "OmniRig radio slot" }
                    SpinBox { from: 1; to: 2; value: appSettings.omniRigSlot; onValueModified: appSettings.omniRigSlot = value }
                    Label { text: "CAT4OM Control URL"; visible: appSettings.frequencyBackendIndex === 2 }
                    TextField { Layout.fillWidth: true; visible: appSettings.frequencyBackendIndex === 2; text: appSettings.cat4omUrl; placeholderText: "ws://127.0.0.1:5001/"; onEditingFinished: appSettings.cat4omUrl = text }
                    Label { text: "CAT4OM radio ID"; visible: appSettings.frequencyBackendIndex === 2 }
                    TextField { Layout.fillWidth: true; visible: appSettings.frequencyBackendIndex === 2; text: appSettings.cat4omRadioId; placeholderText: "Empty selects the first visible radio"; onEditingFinished: appSettings.cat4omRadioId = text }
                    Label { text: "CAT4OM password"; visible: appSettings.frequencyBackendIndex === 2 }
                    TextField { Layout.fillWidth: true; visible: appSettings.frequencyBackendIndex === 2; echoMode: TextInput.Password; placeholderText: "Session only — never saved"; onTextEdited: appSettings.cat4omPassword = text }
                    Label { text: "CAT4OM connection"; visible: appSettings.frequencyBackendIndex === 2 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: appSettings.frequencyBackendIndex === 2
                        Label { text: appSettings.cat4omState; color: "#91a0b1"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { text: appSettings.cat4omFrequencySummary; color: "#43c6ac" }
                        RowLayout {
                            Button { text: "Test read-only"; onClicked: appSettings.testCat4omConnection() }
                            Button { text: "Connect control"; onClicked: appSettings.connectCat4omControl() }
                            Button { text: "Request ownership"; enabled: !appSettings.cat4omCanWrite; onClicked: appSettings.requestCat4omOwnership() }
                            Button { text: "Disconnect"; onClicked: appSettings.disconnectCat4om() }
                        }
                    }
                    Label { text: "CAT port" }
                    ComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: appSettings.serialPorts
                        currentIndex: find(appSettings.catPort)
                        displayText: currentIndex >= 0 ? currentText : appSettings.catPort
                        onActivated: appSettings.catPort = currentText
                        onAccepted: appSettings.catPort = editText
                    }
                    Label { text: "Baud rate" }
                    SpinBox { editable: true; from: 300; to: 1000000; value: appSettings.catBaudRate; onValueModified: appSettings.catBaudRate = value }
                    Label { text: "Data bits" }
                    ComboBox { model: [5, 6, 7, 8]; currentIndex: appSettings.catDataBits - 5; onActivated: appSettings.catDataBits = currentValue }
                    Label { text: "Parity" }
                    ComboBox { model: ["None", "Even", "Odd"]; currentIndex: appSettings.catParityIndex; onActivated: appSettings.catParityIndex = currentIndex }
                    Label { text: "Stop bits" }
                    ComboBox { model: [1, 2]; currentIndex: appSettings.catStopBits - 1; onActivated: appSettings.catStopBits = currentValue }
                    Label { text: "RTS mode" }
                    ComboBox { model: ["Low / no flow control", "Hardware handshake"]; currentIndex: appSettings.catFlowControlIndex; onActivated: appSettings.catFlowControlIndex = currentIndex }
                    Label { text: "Polling interval (ms)" }
                    SpinBox { editable: true; from: 50; to: 10000; value: appSettings.pollIntervalMs; onValueModified: appSettings.pollIntervalMs = value }
                    Label { text: "Timeout (ms)" }
                    SpinBox { editable: true; from: 100; to: 60000; value: appSettings.timeoutMs; onValueModified: appSettings.timeoutMs = value }
                    Label { text: "Split operation" }
                    CheckBox { text: "Use independent TX VFO"; checked: appSettings.splitEnabled; onToggled: appSettings.splitEnabled = checked }
                    Label { text: "RX transverter offset (Hz)" }
                    TextField { Layout.fillWidth: true; text: appSettings.rxTransverterOffsetHz.toString(); placeholderText: "Signed value, e.g. 116000000"; onEditingFinished: appSettings.rxTransverterOffsetHz = Number(text) }
                    Label { text: "TX transverter offset (Hz)" }
                    TextField { Layout.fillWidth: true; text: appSettings.txTransverterOffsetHz.toString(); placeholderText: "Signed value, e.g. 407000000"; onEditingFinished: appSettings.txTransverterOffsetHz = Number(text) }
                    Label { text: "" }
                    RowLayout {
                        Button { text: "Refresh ports"; onClicked: appSettings.refreshSerialPorts() }
                        Button { text: "Configure OmniRig"; enabled: appSettings.omniRigAvailable; onClicked: appSettings.showOmniRigConfiguration() }
                        Button { text: "Restore radio defaults"; onClicked: appSettings.resetToReferenceDefaults() }
                    }
                    Label { text: "" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "All direct serial values are editable. CAT4OM uses its group Control URL and radio ID instead; its password is held for one connection attempt only and is never saved."
                    }
                }
            }

            ScrollView {
                contentWidth: availableWidth
                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 18
                    rowSpacing: 12
                    anchors.margins: 22
                    Label { text: "Direct key/PTT port" }
                    ComboBox {
                        Layout.fillWidth: true
                        editable: true
                        model: appSettings.serialPorts
                        currentIndex: find(appSettings.keyingPort)
                        displayText: currentIndex >= 0 ? currentText : appSettings.keyingPort
                        onActivated: appSettings.keyingPort = currentText
                        onAccepted: appSettings.keyingPort = editText
                    }
                    Label { text: "PTT line" }
                    ComboBox { model: ["RTS", "DTR"]; currentIndex: appSettings.pttLineIndex; onActivated: appSettings.pttLineIndex = currentIndex }
                    Label { text: "KEY line" }
                    ComboBox { model: ["RTS", "DTR"]; currentIndex: appSettings.keyLineIndex; onActivated: appSettings.keyLineIndex = currentIndex }
                    Label { text: "PTT polarity" }
                    CheckBox { text: checked ? "Active high" : "Active low"; checked: appSettings.pttActiveHigh; onToggled: appSettings.pttActiveHigh = checked }
                    Label { text: "KEY polarity" }
                    CheckBox { text: checked ? "Active high" : "Active low"; checked: appSettings.keyActiveHigh; onToggled: appSettings.keyActiveHigh = checked }
                    Label { text: "" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#f3bd55"
                        text: "Ports are enumerated without opening them. Applying settings never asserts PTT or KEY, and transmission remains disarmed."
                    }
                }
            }

            ScrollView {
                contentWidth: availableWidth
                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 18
                    rowSpacing: 12
                    anchors.margins: 22
                    Label { text: "Target FPS" }
                    SpinBox { from: 10; to: 120; value: appSettings.targetFps; onValueModified: appSettings.targetFps = value }
                    Label { text: "Waterfall lines / second" }
                    SpinBox { from: 1; to: 120; value: appSettings.waterfallRate; onValueModified: appSettings.waterfallRate = value }
                    Label { text: "Dynamic range" }
                    CheckBox { text: "Automatic"; checked: appSettings.automaticRange; onToggled: appSettings.automaticRange = checked }
                    Label { text: "Lower bound (dB)" }
                    SpinBox { editable: true; from: -200; to: 50; value: appSettings.lowerBoundDb; enabled: !appSettings.automaticRange; onValueModified: appSettings.lowerBoundDb = value }
                    Label { text: "Upper bound (dB)" }
                    SpinBox { editable: true; from: -190; to: 100; value: appSettings.upperBoundDb; enabled: !appSettings.automaticRange; onValueModified: appSettings.upperBoundDb = value }
                    Label { text: "Spectrum averaging" }
                    SpinBox { from: 1; to: 32; value: appSettings.averagingFrames; onValueModified: appSettings.averagingFrames = value }
                    Label { text: "Reference grid" }
                    CheckBox { text: "Show frequency and level grid"; checked: appSettings.showGrid; onToggled: appSettings.showGrid = checked }
                    Label { text: "" }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#91a0b1"; text: "FPS controls presentation redraws; waterfall rate controls accepted history rows. Averaging is applied in DSP. Additional palettes, peak decay, labels, font size, zoom, pan, and accessibility settings will use this same versioned profile." }
                }
            }

            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "Station configuration profile"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Label { text: appSettings.profileName; font.pixelSize: 15 }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Open Profiles from the main toolbar to create or select another station. For dedicated shortcuts or services, launch with --profile \"name\". Separate processes can use separate profiles and radios."
                    }
                    Button { text: "Run setup helper again"; onClicked: root.setupRequested() }
                }
            }

            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "About CW Assistant"; font.pixelSize: 22; font.weight: Font.DemiBold }
                    Label { text: "Version " + Qt.application.version; color: "#91a0b1" }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2b3541" }
                    Label { text: "Author"; color: "#8290a0" }
                    Label { text: "Alessio Bravi (IU0LFQ / AD2FC)"; font.pixelSize: 17; font.weight: Font.Medium }
                    Label { text: "Author Website"; color: "#8290a0" }
                    Button {
                        text: "https://iu0lfq.it/"
                        flat: true
                        onClicked: Qt.openUrlExternally("https://iu0lfq.it/")
                    }
                    Label { text: "License"; color: "#8290a0" }
                    Label { text: "GNU General Public License v3.0 or later"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Cross-platform multichannel amateur-radio CW receiver and operator assistant."
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: "#2b3541" }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Label { text: appSettings.statusMessage; color: "#91a0b1"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: "Apply"; highlighted: true; onClicked: appSettings.apply() }
        }
    }
}
