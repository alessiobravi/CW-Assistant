import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

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
                Label { text: "No source connected"; color: "#8d9aaa" }
                Button { text: "Choose input"; onClicked: settingsDrawer.open() }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 230
                radius: 10
                color: "#111822"
                border.color: "#263241"
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Label { Layout.alignment: Qt.AlignHCenter; text: "Spectrum"; font.pixelSize: 16 }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Connect an audio or SDR source to begin"
                        color: "#8290a0"
                    }
                }
                Label {
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    anchors.margins: 12
                    text: appSettings.lowerBoundDb.toFixed(0) + " … " + appSettings.upperBoundDb.toFixed(0) + " dB"
                    color: "#718092"
                    font.pixelSize: 11
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#0b121b"
                border.color: "#263241"
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 8
                    Label { Layout.alignment: Qt.AlignHCenter; text: "Waterfall and decoded channels"; font.pixelSize: 16 }
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Tracked callsigns and channel detail cards will appear here"
                        color: "#8290a0"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Label { text: appSettings.statusMessage; color: "#91a0b1"; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: appSettings.targetFps + " FPS target  •  " + appSettings.waterfallRate + " lines/s"; color: "#667789" }
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
