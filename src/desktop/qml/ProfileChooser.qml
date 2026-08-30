import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    width: 520
    height: 520
    modal: true
    closePolicy: appSettings.profileSelectionRequired ? Popup.NoAutoClose : Popup.CloseOnEscape
    title: "Choose station profile"

    contentItem: ColumnLayout {
        spacing: 14
        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "Each profile keeps its own radio, serial, audio, display, logger, and remote-operation settings. Different application instances can use different profiles."
            color: "#91a0b1"
        }
        ListView {
            id: profileList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: appSettings.availableProfiles
            currentIndex: Math.max(0, appSettings.availableProfiles.indexOf(appSettings.profileName))
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: profileList.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: profileList.currentIndex = index
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextField { id: newProfileName; Layout.fillWidth: true; placeholderText: "New profile name" }
            Button {
                text: "Create"
                enabled: newProfileName.text.trim().length > 0
                onClicked: {
                    if (appSettings.createProfile(newProfileName.text)) {
                        newProfileName.clear()
                        root.close()
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: appSettings.statusMessage; color: "#91a0b1"; Layout.fillWidth: true; elide: Text.ElideRight }
            Button {
                text: "Use selected"
                highlighted: true
                enabled: profileList.currentIndex >= 0
                onClicked: {
                    if (appSettings.selectProfile(appSettings.availableProfiles[profileList.currentIndex]))
                        root.close()
                }
            }
        }
    }
}
