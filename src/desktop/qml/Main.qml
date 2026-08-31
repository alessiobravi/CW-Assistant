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
                Label { text: appSettings.referenceRigNames[appSettings.referenceRigIndex]; color: "#8d9aaa"; font.pixelSize: 11 }
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
                Item { Layout.fillWidth: true }
                Label {
                    text: replayController.sourceLoaded
                          ? replayController.sourceName + "  •  " + replayController.sampleRate.toFixed(0) + " Hz"
                          : "No replay source"
                    color: "#8d9aaa"
                }
                Button { text: "Open WAV"; onClicked: wavDialog.open() }
                Button {
                    text: replayController.playing ? "Pause" : "Play"
                    enabled: replayController.sourceLoaded
                    onClicked: replayController.playing ? replayController.pause() : replayController.play()
                }
                Button { text: "Stop"; enabled: replayController.sourceLoaded; onClicked: replayController.stop() }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#09111a"
                border.color: "#263241"

                SpectrumWaterfall {
                    id: spectrumDisplay
                    anchors.fill: parent
                    anchors.margins: 10
                    source: replayController
                    targetFps: appSettings.targetFps
                    waterfallRate: appSettings.waterfallRate
                    automaticRange: appSettings.automaticRange
                    lowerBoundDb: appSettings.lowerBoundDb
                    upperBoundDb: appSettings.upperBoundDb
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
                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 14
                    text: spectrumDisplay.lowerFrequencyHz.toFixed(0) + " Hz"
                    color: "#8394a6"
                    font.pixelSize: 11
                }
                Label {
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 14
                    text: spectrumDisplay.upperFrequencyHz.toFixed(0) + " Hz"
                    color: "#8394a6"
                    font.pixelSize: 11
                }
                ColumnLayout {
                    anchors.centerIn: parent
                    visible: !replayController.sourceLoaded
                    Label { Layout.alignment: Qt.AlignHCenter; text: "Replay a receiver recording"; font.pixelSize: 17 }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Open a PCM or 32-bit float WAV file to inspect its real spectrum and waterfall"
                        color: "#8290a0"
                    }
                    Button { Layout.alignment: Qt.AlignHCenter; text: "Choose WAV recording"; onClicked: wavDialog.open() }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: replayController.statusText; color: "#91a0b1"; elide: Text.ElideRight; Layout.preferredWidth: 300 }
                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(0.001, replayController.durationSeconds)
                    value: replayController.positionSeconds
                }
                Label {
                    text: replayController.positionSeconds.toFixed(1) + " / "
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
                Label { Layout.alignment: Qt.AlignHCenter; text: "No decoded callsigns"; color: "#667789" }
                Item { Layout.fillHeight: true }
            }
        }
    }

    Drawer {
        id: settingsDrawer
        edge: Qt.RightEdge
        width: Math.min(window.width * 0.62, 860)
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
