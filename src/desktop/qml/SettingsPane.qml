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
            TabButton { text: "Audio" }
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
            Layout.leftMargin: 26
            Layout.rightMargin: 26
            Layout.bottomMargin: 8

            ScrollView {
                contentWidth: availableWidth
                GridLayout {
                    width: parent.width
                    columns: 2
                    columnSpacing: 18
                    rowSpacing: 12
                    anchors.margins: 22
                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Choose the sound-card input carrying receiver audio. The selection is independent of radio control and is used in radio and SWL profiles."
                    }
                    Label { text: "Audio input" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: appSettings.audioInputNames
                        currentIndex: appSettings.audioInputIndex
                        onActivated: appSettings.selectAudioInput(currentIndex)
                    }
                    Label { text: "Selected input" }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: appSettings.audioInputDisplayName; color: "#43c6ac" }
                    Label { text: "Radio audio association" }
                    CheckBox {
                        text: "This input carries RX audio from the configured radio"
                        checked: appSettings.audioInputRadioLinked
                        enabled: appSettings.radioEnabled
                        onToggled: appSettings.audioInputRadioLinked = checked
                    }
                    Label { text: "" }
                    Button { text: "Refresh audio inputs"; onClicked: appSettings.refreshAudioInputs() }
                    Label { text: "DC rejection" }
                    CheckBox {
                        objectName: "audioDcRejectionCheck"
                        text: "Remove input DC offset"
                        checked: appSettings.audioDcRejection
                        onToggled: appSettings.audioDcRejection = checked
                    }
                    Label { text: "Software input gain" }
                    CheckBox {
                        objectName: "audioAutomaticGainCheck"
                        text: "Automatic gain"
                        checked: appSettings.audioAutomaticGain
                        onToggled: appSettings.audioAutomaticGain = checked
                    }
                    Label { text: "Manual gain (dB)" }
                    SpinBox {
                        editable: true
                        from: -40
                        to: 40
                        value: Math.round(appSettings.audioGainDb)
                        enabled: !appSettings.audioAutomaticGain
                        onValueModified: appSettings.audioGainDb = value
                    }
                    Label { text: "Automatic target (dBFS)" }
                    SpinBox {
                        editable: true
                        from: -40
                        to: -1
                        value: Math.round(appSettings.audioAutomaticGainTargetDbfs)
                        enabled: appSettings.audioAutomaticGain
                        onValueModified: appSettings.audioAutomaticGainTargetDbfs = value
                    }
                    Label { text: "Processing bandwidth" }
                    CheckBox {
                        objectName: "audioAutomaticBandwidthCheck"
                        text: "Automatic from audio sample rate"
                        checked: appSettings.audioAutomaticBandwidth
                        onToggled: appSettings.audioAutomaticBandwidth = checked
                    }
                    Label { text: "Lower frequency (Hz)" }
                    SpinBox {
                        editable: true
                        from: 0
                        to: 95950
                        value: Math.round(appSettings.audioLowerFrequencyHz)
                        enabled: !appSettings.audioAutomaticBandwidth
                        onValueModified: appSettings.audioLowerFrequencyHz = value
                    }
                    Label { text: "Upper frequency (Hz)" }
                    SpinBox {
                        editable: true
                        from: 50
                        to: 96000
                        value: Math.round(appSettings.audioUpperFrequencyHz)
                        enabled: !appSettings.audioAutomaticBandwidth
                        onValueModified: appSettings.audioUpperFrequencyHz = value
                    }
                    Label { text: "" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#f3bd55"
                        text: "DC rejection removes the persistent zero-frequency peak. Software gain is optional and does not alter the operating-system mixer; when automatic gain is disabled, the manual dB value is exact. Automatic bandwidth derives a 100–3000 Hz CW-oriented view from the input sample rate."
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
                    Label { text: "Radio participation" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["No radio — receive-only (SWL)", "Radio enabled"]
                        currentIndex: appSettings.radioEnabled ? 1 : 0
                        onActivated: appSettings.radioEnabled = currentIndex === 1
                    }
                    Label { text: "Detected online radio" }
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            Layout.fillWidth: true
                            model: appSettings.detectedRadioNames
                            enabled: count > 0
                            currentIndex: appSettings.detectedRadioIndex
                            displayText: count > 0 ? currentText : "None detected"
                            onActivated: appSettings.selectDetectedRadio(currentIndex)
                        }
                        Button { text: "Refresh"; onClicked: appSettings.refreshDetectedRadios() }
                    }
                    Label { text: "Manual radio template" }
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
                    Label { text: "CW audio-to-RF mapping" }
                    ComboBox {
                        Layout.fillWidth: true
                        model: ["CW-U / USB: RF rises with audio tone", "CW-L / LSB: RF falls with audio tone"]
                        currentIndex: appSettings.cwToneSidebandIndex
                        onActivated: appSettings.cwToneSidebandIndex = currentIndex
                    }
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
                        text: appSettings.radioEnabled
                              ? "Detected radios are positively identified by an integration; serial ports are never guessed. Manual CAT values remain editable. CAT4OM passwords are held for one connection attempt only and are never saved."
                              : "SWL mode processes receiver audio without CAT or key/PTT. Stored radio values are retained in case this profile is switched back to radio operation."
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
                    Label { text: "Waterfall history (seconds)" }
                    SpinBox { from: 5; to: 30; value: appSettings.waterfallTimeSpanSeconds; onValueModified: appSettings.waterfallTimeSpanSeconds = value }
                    Label { text: "Visual CW reference" }
                    CheckBox { text: "Show red visual boundaries"; checked: appSettings.showCwGuide; onToggled: appSettings.showCwGuide = checked }
                    Label { text: "Visual center tone (Hz)" }
                    SpinBox { editable: true; from: 0; to: 96000; value: appSettings.cwGuideCenterHz; enabled: appSettings.showCwGuide; onValueModified: appSettings.cwGuideCenterHz = value }
                    Label { text: "Visual width (Hz)" }
                    SpinBox { editable: true; from: 10; to: 5000; value: appSettings.cwGuideWidthHz; enabled: appSettings.showCwGuide; onValueModified: appSettings.cwGuideWidthHz = value }
                    Label { text: "Display level range" }
                    CheckBox { text: "Automatic display scaling"; checked: appSettings.automaticRange; onToggled: appSettings.automaticRange = checked }
                    Label { text: "Lower bound (dB)" }
                    SpinBox { editable: true; from: -200; to: 50; value: appSettings.lowerBoundDb; enabled: !appSettings.automaticRange; onValueModified: appSettings.lowerBoundDb = value }
                    Label { text: "Upper bound (dB)" }
                    SpinBox { editable: true; from: -190; to: 50; value: appSettings.upperBoundDb; enabled: !appSettings.automaticRange; onValueModified: appSettings.upperBoundDb = value }
                    Label { text: "Automatic span (dB)" }
                    SpinBox { editable: true; from: 30; to: 100; value: appSettings.automaticRangeSpanDb; enabled: appSettings.automaticRange; onValueModified: appSettings.automaticRangeSpanDb = value }
                    Label { text: "Waterfall noise suppression" }
                    CheckBox { text: "Darken bins near the measured noise floor"; checked: appSettings.waterfallNoiseSuppression; onToggled: appSettings.waterfallNoiseSuppression = checked }
                    Label { text: "Noise margin (dB)" }
                    SpinBox { editable: true; from: 0; to: 30; value: appSettings.waterfallNoiseMarginDb; enabled: appSettings.waterfallNoiseSuppression; onValueModified: appSettings.waterfallNoiseMarginDb = value }
                    Label { text: "Spectrum averaging" }
                    SpinBox { from: 1; to: 32; value: appSettings.averagingFrames; onValueModified: appSettings.averagingFrames = value }
                    Label { text: "Reference grid" }
                    CheckBox { text: "Show frequency and level grid"; checked: appSettings.showGrid; onToggled: appSettings.showGrid = checked }
                    Label { text: "Decoded signal timeout" }
                    RowLayout {
                        SpinBox {
                            editable: true
                            from: 5
                            to: 120
                            value: appSettings.decodedSignalTimeoutSeconds
                            onValueModified: appSettings.decodedSignalTimeoutSeconds = value
                        }
                        Label { text: "seconds"; color: "#8290a0" }
                    }
                    Label { text: "" }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#91a0b1"; text: "Waterfall history is a constant time window: resizing, startup fill, and line density do not stretch or collapse Morse timing. Automatic display scaling uses a stable minimum span so receiver noise stays dark instead of pumping through the palette. Noise suppression affects waterfall colors only; raw spectrum bins remain available to the future decoder. The same operational controls are available directly below the spectrum. A decoded signal's marker and session remain available for the configured timeout after it goes silent, then are removed." }
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
                    Label { text: "Own station callsign"; font.weight: Font.DemiBold }
                    TextField {
                        objectName: "ownCallsignField"
                        Layout.fillWidth: true
                        text: appSettings.ownCallsign
                        placeholderText: "Example: IU0LFQ or AD2FC"
                        maximumLength: 16
                        inputMethodHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
                        onEditingFinished: appSettings.ownCallsign = text
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Saved per station profile. This exact normalized callsign will identify replies addressed to you, populate station logging fields, and drive the planned own-call notification and guarded closing macro."
                    }
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
                    Label {
                        objectName: "aboutVersionLabel"
                        text: "Version " + Qt.application.version
                        color: "#91a0b1"
                    }
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
