import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "setupWizard"
    width: Math.max(420, Math.min(780, parent ? parent.width - 32 : 780))
    height: Math.max(520, Math.min(680, parent ? parent.height - 32 : 680))
    modal: true
    closePolicy: Popup.NoAutoClose
    title: "Station setup — " + appSettings.profileName
    property int step: 0
    property bool manualRadioSetup: appSettings.radioEnabled
    onStepChanged: pageFlick.contentY = 0
    Component.onCompleted: appSettings.refreshDetectedRadios()

    function goForward() {
        if (root.step === 0 && !appSettings.radioEnabled)
            root.step = 3
        else
            root.step++
    }

    function goBack() {
        if (root.step === 3 && !appSettings.radioEnabled)
            root.step = 0
        else
            root.step--
    }

    footer: Rectangle {
        implicitHeight: footerLayout.implicitHeight + 24
        color: "#151b23"
        border.color: "#2b3541"

        RowLayout {
            id: footerLayout
            anchors.fill: parent
            anchors.margins: 12
            Label { text: appSettings.statusMessage; color: "#91a0b1"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button { text: "Cancel"; visible: appSettings.setupComplete; onClicked: { root.step = 0; root.close() } }
            Button { text: "Back"; enabled: root.step > 0; onClicked: root.goBack() }
            Button {
                objectName: "setupNextButton"
                text: root.step < 4 ? "Next" : "Finish"
                highlighted: true
                onClicked: {
                    if (root.step < 4)
                        root.goForward()
                    else if (appSettings.completeSetup()) {
                        root.step = 0
                        root.close()
                    }
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: ["Radio", "CAT", "Keying", "Display", "Review"]
                delegate: Label {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData
                    color: index <= root.step ? "#43c6ac" : "#667789"
                    font.weight: index === root.step ? Font.Bold : Font.Normal
                }
            }
        }
        ProgressBar { Layout.fillWidth: true; from: 0; to: 4; value: root.step }

        Flickable {
            id: pageFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            contentWidth: width
            contentHeight: Math.max(height, pageStack.implicitHeight)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            StackLayout {
                id: pageStack
                currentIndex: root.step
                width: Math.max(0, pageFlick.width - 14)
                height: Math.max(pageFlick.height, implicitHeight)

                ColumnLayout {
                spacing: 16
                Label { text: "Choose the receiver setup"; font.pixelSize: 22; font.weight: Font.DemiBold }
                RadioButton {
                    text: "No radio — receive-only audio decoding (SWL)"
                    checked: !appSettings.radioEnabled
                    onClicked: {
                        appSettings.radioEnabled = false
                        root.manualRadioSetup = false
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#91a0b1"
                    text: "SWL mode skips CAT and direct key/PTT setup. It never opens a radio port and cannot transmit."
                }
                Label { text: "Detected online radios"; font.weight: Font.DemiBold }
                ComboBox {
                    Layout.fillWidth: true
                    model: appSettings.detectedRadioNames
                    enabled: count > 0
                    currentIndex: appSettings.detectedRadioIndex
                    displayText: count > 0 ? currentText : "No positively identified online radio found"
                    onActivated: {
                        appSettings.selectDetectedRadio(currentIndex)
                        root.manualRadioSetup = false
                    }
                }
                RowLayout {
                    Button { text: "Refresh detection"; onClicked: appSettings.refreshDetectedRadios() }
                    Button {
                        text: root.manualRadioSetup ? "Hide manual setup" : "Set up a radio manually"
                        checkable: true
                        checked: root.manualRadioSetup
                        onClicked: {
                            root.manualRadioSetup = checked
                            if (checked)
                                appSettings.radioEnabled = true
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#91a0b1"
                    visible: root.manualRadioSetup
                    text: "Manual radio template (all CAT values remain editable)"
                }
                ComboBox {
                    Layout.fillWidth: true
                    visible: root.manualRadioSetup
                    model: appSettings.referenceRigNames
                    currentIndex: appSettings.referenceRigIndex
                    onActivated: appSettings.selectReferenceRig(currentIndex)
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#91a0b1"
                    text: "Serial ports are not presented as radios: a port name cannot safely identify the connected model. Detection uses an installed integration and lists only radios reporting an online state."
                }
                Item { Layout.fillHeight: true }
            }

                GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 12
                Label { text: "Frequency provider" }
                ComboBox { Layout.fillWidth: true; model: ["OmniRig (Windows)", "Hamlib", "CAT4OM network service"]; currentIndex: appSettings.frequencyBackendIndex; onActivated: appSettings.frequencyBackendIndex = currentIndex }
                Label { text: "OmniRig slot" }
                SpinBox { from: 1; to: 2; value: appSettings.omniRigSlot; onValueModified: appSettings.omniRigSlot = value }
                Label { text: "CAT4OM Control URL"; visible: appSettings.frequencyBackendIndex === 2 }
                TextField { Layout.fillWidth: true; visible: appSettings.frequencyBackendIndex === 2; text: appSettings.cat4omUrl; placeholderText: "ws://127.0.0.1:5001/"; onEditingFinished: appSettings.cat4omUrl = text }
                Label { text: "CAT4OM radio ID"; visible: appSettings.frequencyBackendIndex === 2 }
                TextField { Layout.fillWidth: true; visible: appSettings.frequencyBackendIndex === 2; text: appSettings.cat4omRadioId; placeholderText: "Optional"; onEditingFinished: appSettings.cat4omRadioId = text }
                Label { text: "CAT COM port" }
                ComboBox { Layout.fillWidth: true; editable: true; model: appSettings.serialPorts; currentIndex: find(appSettings.catPort); displayText: currentIndex >= 0 ? currentText : appSettings.catPort; onActivated: appSettings.catPort = currentText; onAccepted: appSettings.catPort = editText }
                Label { text: "Baud rate" }
                SpinBox { editable: true; from: 300; to: 1000000; value: appSettings.catBaudRate; onValueModified: appSettings.catBaudRate = value }
                Label { text: "Framing" }
                RowLayout {
                    ComboBox { model: [5, 6, 7, 8]; currentIndex: appSettings.catDataBits - 5; onActivated: appSettings.catDataBits = currentValue }
                    ComboBox { model: ["None", "Even", "Odd"]; currentIndex: appSettings.catParityIndex; onActivated: appSettings.catParityIndex = currentIndex }
                    ComboBox { model: [1, 2]; currentIndex: appSettings.catStopBits - 1; onActivated: appSettings.catStopBits = currentValue }
                }
                Label { text: "RTS mode" }
                ComboBox { model: ["Low / none", "Handshake"]; currentIndex: appSettings.catFlowControlIndex; onActivated: appSettings.catFlowControlIndex = currentIndex }
                Label { text: "Split" }
                CheckBox { text: "Independent TX frequency"; checked: appSettings.splitEnabled; onToggled: appSettings.splitEnabled = checked }
                Label { text: "RX transverter offset (Hz)" }
                TextField { Layout.fillWidth: true; text: appSettings.rxTransverterOffsetHz.toString(); onEditingFinished: appSettings.rxTransverterOffsetHz = Number(text) }
                Label { text: "TX transverter offset (Hz)" }
                TextField { Layout.fillWidth: true; text: appSettings.txTransverterOffsetHz.toString(); onEditingFinished: appSettings.txTransverterOffsetHz = Number(text) }
                Label { text: "" }
                RowLayout {
                    Button { text: "Refresh ports"; onClicked: appSettings.refreshSerialPorts() }
                    Button { text: "Configure OmniRig"; enabled: appSettings.omniRigAvailable; onClicked: appSettings.showOmniRigConfiguration() }
                }
            }

                GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 12
                Label { Layout.columnSpan: 2; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#f3bd55"; text: "Select the dedicated direct-COM interface. Port discovery is passive and does not toggle either line." }
                Label { text: "Key/PTT COM port" }
                ComboBox { Layout.fillWidth: true; editable: true; model: appSettings.serialPorts; currentIndex: find(appSettings.keyingPort); displayText: currentIndex >= 0 ? currentText : appSettings.keyingPort; onActivated: appSettings.keyingPort = currentText; onAccepted: appSettings.keyingPort = editText }
                Label { text: "PTT" }
                RowLayout {
                    ComboBox { model: ["RTS", "DTR"]; currentIndex: appSettings.pttLineIndex; onActivated: appSettings.pttLineIndex = currentIndex }
                    CheckBox { text: "Active high"; checked: appSettings.pttActiveHigh; onToggled: appSettings.pttActiveHigh = checked }
                }
                Label { text: "KEY" }
                RowLayout {
                    ComboBox { model: ["RTS", "DTR"]; currentIndex: appSettings.keyLineIndex; onActivated: appSettings.keyLineIndex = currentIndex }
                    CheckBox { text: "Active high"; checked: appSettings.keyActiveHigh; onToggled: appSettings.keyActiveHigh = checked }
                }
                Label { Layout.columnSpan: 2; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#91a0b1"; text: "A hardware loopback test and maximum-key-down watchdog will be required before this profile can be armed for transmission." }
            }

                GridLayout {
                columns: 2
                columnSpacing: 18
                rowSpacing: 12
                Label { text: "Spectrum target FPS" }
                SpinBox { from: 10; to: 120; value: appSettings.targetFps; onValueModified: appSettings.targetFps = value }
                Label { text: "Waterfall lines / second" }
                SpinBox { from: 1; to: 120; value: appSettings.waterfallRate; onValueModified: appSettings.waterfallRate = value }
                Label { text: "Range" }
                CheckBox { text: "Automatic dynamic range"; checked: appSettings.automaticRange; onToggled: appSettings.automaticRange = checked }
                Label { Layout.columnSpan: 2; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#91a0b1"; text: "These are starting values. The full Settings pane provides independent visualization controls and future startup calibration recommendations." }
            }

                ColumnLayout {
                spacing: 14
                Label { text: "Ready to create this station profile"; font.pixelSize: 22; font.weight: Font.DemiBold }
                Label { text: "Profile: " + appSettings.profileName }
                Label { text: "Radio: " + appSettings.radioDisplayName }
                Label { text: appSettings.radioEnabled ? "CAT: " + (appSettings.catPort || "not selected") + " • " + appSettings.catBaudRate + " baud" : "CAT: skipped in SWL mode" }
                Label { text: appSettings.radioEnabled ? "Direct key/PTT: " + (appSettings.keyingPort || "not selected") : "Direct key/PTT: disabled in SWL mode" }
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#f3bd55"; text: "Finishing saves configuration only. It does not open ports, arm the transmitter, or send a test signal." }
                Item { Layout.fillHeight: true }
            }
        }
    }
}
}
