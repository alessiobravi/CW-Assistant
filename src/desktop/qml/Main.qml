import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Dialogs
import CWAssistant 1.0

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    visible: true
    title: "CW Assistant — " + appSettings.profileName
    color: "#0d1117"
    Material.theme: Material.Dark
    Material.accent: "#43c6ac"

    function formatFrequency(hz) {
        if (Math.abs(hz) >= 1000000)
            return (hz / 1000000).toFixed(3) + " MHz"
        if (Math.abs(hz) >= 10000)
            return (hz / 1000).toFixed(1) + " kHz"
        return hz.toFixed(0) + " Hz"
    }

    header: ToolBar {
        height: 64
        background: Rectangle { color: "#151b23" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 20
            anchors.rightMargin: 16
            spacing: 16
            Label {
                text: "CW Assistant"
                font.pixelSize: 21
                font.weight: Font.DemiBold
            }
            Rectangle { width: 1; height: 28; color: "#303a46" }
            ColumnLayout {
                spacing: 0
                Label { text: appSettings.profileName; font.pixelSize: 14 }
                Label { text: appSettings.radioDisplayName; color: "#8d9aaa"; font.pixelSize: 11 }
            }
            Item { Layout.fillWidth: true }
            Rectangle {
                implicitWidth: safeLabel.implicitWidth + 24
                implicitHeight: 30
                radius: 15
                color: "#202833"
                Label {
                    id: safeLabel
                    anchors.centerIn: parent
                    text: "TX DISARMED"
                    color: "#f3bd55"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
            ToolButton { text: "Profiles"; onClicked: profileChooser.open() }
            ToolButton { text: "Settings"; onClicked: settingsDrawer.open() }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: 76
            Layout.fillHeight: true
            color: "#111720"
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 18
                spacing: 12
                Repeater {
                    model: ["RX", "CALLS", "QSO", "LOG", "REMOTE"]
                    delegate: Button {
                        required property string modelData
                        width: 60
                        height: 44
                        flat: true
                        text: modelData
                        font.pixelSize: 10
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 14
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Label { text: "Receiver workspace"; font.pixelSize: 18; font.weight: Font.DemiBold }
                ComboBox {
                    model: ["Live audio", "WAV replay"]
                    currentIndex: replayController.sourceMode
                    onActivated: replayController.sourceMode = currentIndex
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: replayController.sourceMode === 0
                          ? appSettings.audioInputDisplayName
                          : (replayController.sourceLoaded
                             ? replayController.sourceName + "  •  " + replayController.sampleRate.toFixed(0) + " Hz"
                             : "No replay source")
                    color: "#8d9aaa"
                }
                Button {
                    objectName: "startLiveAudioButton"
                    text: "Start live RX"
                    visible: replayController.sourceMode === 0
                    enabled: !replayController.liveCapturing
                    onClicked: replayController.startLiveAudio()
                }
                Button {
                    text: "Stop live RX"
                    visible: replayController.sourceMode === 0
                    enabled: replayController.liveCapturing
                    onClicked: replayController.stopLiveAudio()
                }
                Button { text: "Open WAV"; visible: replayController.sourceMode === 1; onClicked: wavDialog.open() }
                Button {
                    text: replayController.playing ? "Pause" : "Play"
                    visible: replayController.sourceMode === 1
                    enabled: replayController.sourceLoaded
                    onClicked: replayController.playing ? replayController.pause() : replayController.play()
                }
                Button { text: "Stop"; visible: replayController.sourceMode === 1; enabled: replayController.sourceLoaded; onClicked: replayController.stop() }
            }

            Rectangle {
                id: spectrumPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#09111a"
                border.color: "#263241"

                SpectrumWaterfall {
                    id: spectrumDisplay
                    objectName: "spectrumDisplay"
                    anchors.fill: parent
                    anchors.margins: 10
                    source: replayController
                    targetFps: appSettings.targetFps
                    waterfallRate: appSettings.waterfallRate
                    waterfallTimeSpanSeconds: appSettings.waterfallTimeSpanSeconds
                    automaticRange: appSettings.automaticRange
                    automaticRangeSpanDb: appSettings.automaticRangeSpanDb
                    lowerBoundDb: appSettings.lowerBoundDb
                    upperBoundDb: appSettings.upperBoundDb
                    noiseSuppression: appSettings.waterfallNoiseSuppression
                    noiseMarginDb: appSettings.waterfallNoiseMarginDb
                    showGrid: appSettings.showGrid
                }

                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.margins: 14
                    text: spectrumDisplay.effectiveUpperBoundDb.toFixed(0) + " dBFS"
                    color: "#8394a6"
                    font.pixelSize: 11
                }
                Label {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: parent.height * 0.34
                    anchors.leftMargin: 14
                    text: spectrumDisplay.effectiveLowerBoundDb.toFixed(0) + " dBFS"
                    color: "#8394a6"
                    font.pixelSize: 11
                }
                Repeater {
                    model: 7
                    delegate: Item {
                        required property int index
                        property real fraction: index / 6.0
                        property real tickX: spectrumDisplay.x
                                             + fraction * spectrumDisplay.width
                        visible: spectrumDisplay.upperFrequencyHz
                                 > spectrumDisplay.lowerFrequencyHz
                        x: tickX
                        y: spectrumDisplay.y + spectrumDisplay.height - 22
                        z: 4
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 1
                            height: 6
                            color: "#75879a"
                        }
                        Label {
                            x: index === 0 ? 2
                               : (index === 6 ? -implicitWidth - 2
                                  : -implicitWidth / 2)
                            y: 5
                            text: window.formatFrequency(
                                      spectrumDisplay.lowerFrequencyHz
                                      + fraction
                                        * (spectrumDisplay.upperFrequencyHz
                                           - spectrumDisplay.lowerFrequencyHz))
                            color: "#9cabb9"
                            font.pixelSize: 10
                        }
                    }
                }

                Repeater {
                    model: 2
                    delegate: Rectangle {
                        required property int index
                        property real boundaryHz: appSettings.cwGuideCenterHz
                                                  + (index === 0 ? -0.5 : 0.5)
                                                    * appSettings.cwGuideWidthHz
                        visible: appSettings.showCwGuide
                                 && boundaryHz >= spectrumDisplay.lowerFrequencyHz
                                 && boundaryHz <= spectrumDisplay.upperFrequencyHz
                                 && spectrumDisplay.upperFrequencyHz
                                    > spectrumDisplay.lowerFrequencyHz
                        x: spectrumDisplay.x
                           + (boundaryHz - spectrumDisplay.lowerFrequencyHz)
                             / (spectrumDisplay.upperFrequencyHz
                                - spectrumDisplay.lowerFrequencyHz)
                             * spectrumDisplay.width
                        y: spectrumDisplay.y
                        width: 2
                        height: spectrumDisplay.height
                        color: "#ff4d5a"
                        opacity: 0.85
                        z: 3
                    }
                }
                Label {
                    visible: appSettings.showCwGuide
                             && spectrumDisplay.upperFrequencyHz
                                > spectrumDisplay.lowerFrequencyHz
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 14
                    text: "CW guide  " + appSettings.cwGuideCenterHz.toFixed(0)
                          + " Hz  •  " + appSettings.cwGuideWidthHz.toFixed(0)
                          + " Hz wide"
                    color: "#ff7b84"
                    font.pixelSize: 10
                    z: 4
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: !replayController.activeSource
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: replayController.sourceMode === 0 ? "Live receiver audio" : "Replay a receiver recording"
                        font.pixelSize: 17
                    }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: replayController.sourceMode === 0
                              ? "Start live RX to process the selected audio input"
                              : "Open a PCM or 32-bit float WAV file to inspect its real spectrum and waterfall"
                        color: "#8290a0"
                    }
                    Button {
                        Layout.alignment: Qt.AlignHCenter
                        text: replayController.sourceMode === 0 ? "Start live RX" : "Choose WAV recording"
                        onClicked: replayController.sourceMode === 0 ? replayController.startLiveAudio() : wavDialog.open()
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                padding: 8
                background: Rectangle {
                    radius: 8
                    color: "#151b23"
                    border.color: "#2b3541"
                }
                ColumnLayout {
                    width: parent.width
                    spacing: 4
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Live spectrum controls"; font.weight: Font.DemiBold }
                        TabBar {
                            id: liveControlTabs
                            Layout.preferredWidth: 190
                            TabButton { text: "Signal" }
                            TabButton { text: "Display" }
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            objectName: "decoderUnavailableLabel"
                            text: "Decoder unavailable in this build"
                            color: "#f3bd55"
                            font.pixelSize: 11
                        }
                        Button { text: "Save profile"; onClicked: appSettings.apply() }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        currentIndex: liveControlTabs.currentIndex
                        ScrollView {
                            Layout.fillWidth: true
                            implicitHeight: signalControls.implicitHeight + 4
                            contentWidth: signalControls.implicitWidth
                            contentHeight: signalControls.implicitHeight
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            RowLayout {
                                id: signalControls
                                spacing: 8
                            CheckBox {
                                text: "DC rejection"
                                checked: appSettings.audioDcRejection
                                onToggled: appSettings.audioDcRejection = checked
                            }
                            CheckBox {
                                text: "Auto gain"
                                checked: appSettings.audioAutomaticGain
                                onToggled: appSettings.audioAutomaticGain = checked
                            }
                            Label { text: appSettings.audioAutomaticGain ? "Target" : "Gain" }
                            SpinBox {
                                editable: true
                                from: -40
                                to: appSettings.audioAutomaticGain ? -1 : 40
                                value: Math.round(appSettings.audioAutomaticGain
                                                  ? appSettings.audioAutomaticGainTargetDbfs
                                                  : appSettings.audioGainDb)
                                onValueModified: {
                                    if (appSettings.audioAutomaticGain)
                                        appSettings.audioAutomaticGainTargetDbfs = value
                                    else
                                        appSettings.audioGainDb = value
                                }
                            }
                            Label { text: "dB" + (appSettings.audioAutomaticGain ? "FS" : "") }
                            CheckBox {
                                text: "Auto bandwidth"
                                checked: appSettings.audioAutomaticBandwidth
                                onToggled: appSettings.audioAutomaticBandwidth = checked
                            }
                            Label { text: "Low"; visible: !appSettings.audioAutomaticBandwidth }
                            SpinBox {
                                editable: true
                                from: 0
                                to: 95950
                                value: Math.round(appSettings.audioLowerFrequencyHz)
                                visible: !appSettings.audioAutomaticBandwidth
                                onValueModified: appSettings.audioLowerFrequencyHz = value
                            }
                            Label { text: "High"; visible: !appSettings.audioAutomaticBandwidth }
                            SpinBox {
                                editable: true
                                from: 50
                                to: 96000
                                value: Math.round(appSettings.audioUpperFrequencyHz)
                                visible: !appSettings.audioAutomaticBandwidth
                                onValueModified: appSettings.audioUpperFrequencyHz = value
                            }
                            Label { text: "Hz"; visible: !appSettings.audioAutomaticBandwidth }
                            }
                        }
                        ScrollView {
                            Layout.fillWidth: true
                            implicitHeight: displayControls.implicitHeight + 4
                            contentWidth: displayControls.implicitWidth
                            contentHeight: displayControls.implicitHeight
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AlwaysOff }
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            RowLayout {
                                id: displayControls
                                spacing: 8
                            Label { text: "FPS" }
                            SpinBox {
                                from: 10
                                to: 120
                                value: appSettings.targetFps
                                onValueModified: appSettings.targetFps = value
                            }
                            Label { text: "Lines/s" }
                            SpinBox {
                                from: 1
                                to: 120
                                value: appSettings.waterfallRate
                                onValueModified: appSettings.waterfallRate = value
                            }
                            Label { text: "Avg" }
                            SpinBox {
                                from: 1
                                to: 32
                                value: appSettings.averagingFrames
                                onValueModified: appSettings.averagingFrames = value
                            }
                            CheckBox {
                                objectName: "liveAutomaticLevelsCheck"
                                text: "Auto levels"
                                checked: appSettings.automaticRange
                                onToggled: appSettings.automaticRange = checked
                            }
                            Label { text: "Span"; visible: appSettings.automaticRange }
                            SpinBox {
                                editable: true
                                from: 30
                                to: 100
                                value: Math.round(appSettings.automaticRangeSpanDb)
                                visible: appSettings.automaticRange
                                onValueModified: appSettings.automaticRangeSpanDb = value
                            }
                            Label { text: "Floor"; visible: !appSettings.automaticRange }
                            SpinBox {
                                editable: true
                                from: -200
                                to: 40
                                value: Math.round(appSettings.lowerBoundDb)
                                visible: !appSettings.automaticRange
                                onValueModified: appSettings.lowerBoundDb = value
                            }
                            Label { text: "Ceiling"; visible: !appSettings.automaticRange }
                            SpinBox {
                                editable: true
                                from: -190
                                to: 50
                                value: Math.round(appSettings.upperBoundDb)
                                visible: !appSettings.automaticRange
                                onValueModified: appSettings.upperBoundDb = value
                            }
                            Label { text: "History" }
                            SpinBox {
                                editable: true
                                from: 5
                                to: 30
                                value: appSettings.waterfallTimeSpanSeconds
                                onValueModified: appSettings.waterfallTimeSpanSeconds = value
                            }
                            Label { text: "seconds"; color: "#8290a0" }
                            CheckBox {
                                objectName: "liveCwGuideCheck"
                                text: "CW guide"
                                checked: appSettings.showCwGuide
                                onToggled: appSettings.showCwGuide = checked
                            }
                            Label { text: "Tone"; enabled: appSettings.showCwGuide }
                            SpinBox {
                                editable: true
                                from: 0
                                to: 96000
                                value: Math.round(appSettings.cwGuideCenterHz)
                                enabled: appSettings.showCwGuide
                                onValueModified: appSettings.cwGuideCenterHz = value
                            }
                            Label { text: "Width"; enabled: appSettings.showCwGuide }
                            SpinBox {
                                editable: true
                                from: 10
                                to: 5000
                                value: Math.round(appSettings.cwGuideWidthHz)
                                enabled: appSettings.showCwGuide
                                onValueModified: appSettings.cwGuideWidthHz = value
                            }
                            CheckBox {
                                objectName: "liveNoiseSuppressionCheck"
                                text: "Suppress noise"
                                checked: appSettings.waterfallNoiseSuppression
                                onToggled: appSettings.waterfallNoiseSuppression = checked
                            }
                            Label { text: "Margin" }
                            SpinBox {
                                editable: true
                                from: 0
                                to: 30
                                value: Math.round(appSettings.waterfallNoiseMarginDb)
                                enabled: appSettings.waterfallNoiseSuppression
                                onValueModified: appSettings.waterfallNoiseMarginDb = value
                            }
                            Label { text: "dB  •  Noise " + spectrumDisplay.estimatedNoiseFloorDb.toFixed(0) + " dBFS"; color: "#8290a0" }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: replayController.statusText; color: "#91a0b1"; elide: Text.ElideRight; Layout.preferredWidth: 300 }
                ProgressBar {
                    Layout.fillWidth: true
                    visible: replayController.sourceMode === 1
                    from: 0
                    to: Math.max(0.001, replayController.durationSeconds)
                    value: replayController.positionSeconds
                }
                Label {
                    text: replayController.sourceMode === 0
                          ? "Input overruns: " + replayController.inputOverruns + "  •  "
                            + appSettings.targetFps + " FPS  •  " + appSettings.waterfallRate + " rows/s"
                          : replayController.positionSeconds.toFixed(1) + " / "
                            + replayController.durationSeconds.toFixed(1) + " s  •  "
                            + appSettings.targetFps + " FPS  •  " + appSettings.waterfallRate + " rows/s"
                    color: "#667789"
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 330
            Layout.fillHeight: true
            color: "#111720"
            border.color: "#263241"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Label { text: "Active calls"; font.pixelSize: 17; font.weight: Font.DemiBold }
                Label { text: "Signals selected by confidence, strength, or queue"; color: "#8290a0"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#263241" }
                Item { Layout.fillHeight: true }
                Label { Layout.alignment: Qt.AlignHCenter; text: "CW decoder not implemented yet"; color: "#667789" }
                Item { Layout.fillHeight: true }
            }
        }
    }

    Drawer {
        id: settingsDrawer
        edge: Qt.RightEdge
        width: Math.min(window.width * 0.78, 1080)
        height: window.height
        SettingsPane {
            anchors.fill: parent
            onDone: settingsDrawer.close()
            onSetupRequested: setupWizard.open()
        }
    }

    ProfileChooser {
        id: profileChooser
        anchors.centerIn: Overlay.overlay
    }

    SetupWizard {
        id: setupWizard
        anchors.centerIn: Overlay.overlay
    }

    FileDialog {
        id: wavDialog
        title: "Open receiver WAV recording"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV audio (*.wav *.wave)", "All files (*)"]
        onAccepted: replayController.openFile(selectedFile)
    }

    Component.onCompleted: {
        if (appSettings.profileSelectionRequired)
            profileChooser.open()
        else if (!appSettings.setupComplete)
            setupWizard.open()
    }

    Connections {
        target: appSettings
        function onProfileSelectionRequiredChanged() {
            if (!appSettings.profileSelectionRequired && !appSettings.setupComplete)
                setupWizard.open()
        }
        function onProfileChanged() {
            if (!appSettings.setupComplete)
                setupWizard.open()
        }
    }
}
