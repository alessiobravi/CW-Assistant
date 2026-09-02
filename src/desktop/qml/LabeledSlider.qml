import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    required property string caption
    property real from: 0
    property real to: 100
    property real value: 0
    property real stepSize: 1
    property int decimals: 0
    signal moved(real value)

    spacing: 1
    Label {
        text: root.caption + "  " + Number(root.value).toFixed(root.decimals)
        color: root.enabled ? "#c7d2df" : "#667586"
        font.pixelSize: 11
    }
    Slider {
        Layout.fillWidth: true
        from: root.from
        to: root.to
        value: root.value
        stepSize: root.stepSize
        snapMode: Slider.SnapAlways
        onMoved: root.moved(value)
    }
}
