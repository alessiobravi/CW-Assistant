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
                Repeater {
                    model: replayController.decoderChannels
                    delegate: Item {
                        required property var modelData
                        required property int index
                        property real channelHz: modelData.frequencyHz
                        visible: channelHz >= spectrumDisplay.lowerFrequencyHz
                                 && channelHz <= spectrumDisplay.upperFrequencyHz
                                 && spectrumDisplay.upperFrequencyHz
                                    > spectrumDisplay.lowerFrequencyHz
                        x: spectrumDisplay.x
                           + (channelHz - spectrumDisplay.lowerFrequencyHz)
                             / (spectrumDisplay.upperFrequencyHz
                                - spectrumDisplay.lowerFrequencyHz)
                             * spectrumDisplay.width - width / 2
                        y: spectrumDisplay.y
                        width: 28
                        height: spectrumDisplay.height
                        z: 5
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 8
                            height: parent.height
                            color: modelData.color
                            opacity: modelData.active ? 0.12 : 0.05
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: modelData.keyDown ? 3 : 1
                            height: parent.height
                            color: modelData.color
                            opacity: modelData.active ? 0.9 : 0.45
                        }
                        Label {
                            anchors.left: parent.horizontalCenter
                            anchors.leftMargin: 7
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 12
                            transformOrigin: Item.BottomLeft
                            rotation: -90
                            text: (modelData.callsign.length > 0
                                   ? modelData.callsign + "  •  " : "")
                                  + modelData.frequencyLabel
                            color: modelData.color
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                        MouseArea {
                            id: channelHitArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: replayController.openDecoderSession(modelData.id)
                            ToolTip.visible: containsMouse
                            ToolTip.delay: 450
                            ToolTip.text: (modelData.callsign.length > 0
                                           ? modelData.callsign + "\n" : "")
                                          + modelData.frequencyLabel + "\n"
                                          + modelData.audioFrequencyHz.toFixed(1)
                                            + " Hz audio • "
                                          + modelData.filterWidthHz.toFixed(0)
                                            + " Hz filter • "
                                          + modelData.driftHzPerSecond.toFixed(1)
                                            + " Hz/s drift"
                        }
                    }
                }
                Label {
                    visible: appSettings.showCwGuide
                             && spectrumDisplay.upperFrequencyHz
                                > spectrumDisplay.lowerFrequencyHz
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 14
                    text: "Visual guide  " + appSettings.cwGuideCenterHz.toFixed(0)
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
                                text: "Visual guide"
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
            Layout.preferredWidth: 390
            Layout.fillHeight: true
            color: "#111720"
            border.color: "#263241"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                Label { text: "Full-spectrum CW decoder"; font.pixelSize: 17; font.weight: Font.DemiBold }
                Label {
                    text: replayController.decoderChannelCount > 0
                          ? replayController.decoderChannelCount
                            + " signal(s) detected • "
                            + replayController.decoderSessionCount
                            + " session(s) open"
                          : "Scanning the complete processed passband"
                    color: "#8290a0"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: "#263241" }
                Label {
                    visible: replayController.decoderSessionCount === 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: replayController.decoderChannelCount > 0
                          ? "Click a colored signal marker in the spectrum or waterfall to open its decoded session here. Closed sessions continue decoding and can be reopened."
                          : "Listening for CW signals across the spectrum…\n\nThe red CW boundaries are a visual reference only and do not limit decoding."
                    color: "#667789"
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignTop
                }
                ListView {
                    id: decoderChannelList
                    objectName: "decoderChannelList"
                    visible: replayController.decoderSessionCount > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: 8
                    model: replayController.decoderSessions
                    delegate: Rectangle {
                        id: sessionCard
                        required property var modelData
                        required property int index
                        width: decoderChannelList.width
                        height: 116
                        radius: 7
                        color: "#151d27"
                        border.width: modelData.keyDown ? 2 : 1
                        border.color: modelData.color
                        Drag.active: sessionDrag.active
                        Drag.source: sessionCard
                        Drag.hotSpot.x: width / 2
                        Drag.hotSpot.y: height / 2
                        z: sessionDrag.active ? 20 : 1
                        opacity: sessionDrag.active ? 0.78 : 1.0
                        DragHandler { id: sessionDrag }
                        DropArea {
                            anchors.fill: parent
                            onEntered: function(drag) {
                                if (drag.source && drag.source !== sessionCard)
                                    replayController.moveDecoderSession(
                                                drag.source.modelData.id,
                                                sessionCard.index)
                            }
                        }
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            RowLayout {
                                Layout.fillWidth: true
                                Rectangle {
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: modelData.color
                                }
                                Label {
                                    text: modelData.frequencyLabel
                                    color: modelData.color
                                    font.weight: Font.Bold
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: modelData.active ? "ACTIVE" : "HOLD"
                                    color: modelData.active ? modelData.color : "#718091"
                                    font.pixelSize: 10
                                }
                                ToolButton {
                                    text: "×"
                                    Accessible.name: "Close decoded session"
                                    onClicked: replayController.closeDecoderSession(
                                                   modelData.id)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Label {
                                    visible: modelData.text.length > 0
                                    Layout.fillWidth: true
                                    text: modelData.text
                                    color: "#edf3f8"
                                    font.pixelSize: 17
                                    elide: Text.ElideLeft
                                }
                                Label {
                                    visible: modelData.provisionalText.length > 0
                                             || modelData.elements.length > 0
                                    text: modelData.provisionalText.length > 0
                                          ? modelData.provisionalText
                                          : modelData.elements
                                    color: "#e3ad55"
                                    font.pixelSize: 17
                                    font.italic: true
                                }
                                Label {
                                    visible: modelData.text.length === 0
                                             && modelData.provisionalText.length === 0
                                             && modelData.elements.length === 0
                                    Layout.fillWidth: true
                                    text: "Listening…"
                                    color: "#8290a0"
                                    font.pixelSize: 15
                                    font.italic: true
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.wpm.toFixed(1) + " WPM  •  "
                                      + modelData.snrDb.toFixed(1) + " dB SNR  •  "
                                      + modelData.filterWidthHz.toFixed(0)
                                        + " Hz filter  •  "
                                      + (modelData.confidence * 100).toFixed(0)
                                        + "% confidence  •  "
                                      + (modelData.keyProbability * 100).toFixed(0)
                                        + "% key"
                                color: "#8290a0"
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
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
