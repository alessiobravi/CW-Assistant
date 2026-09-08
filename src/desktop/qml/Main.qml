import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material
import QtQuick.Dialogs
import CWBuddy 1.0

ApplicationWindow {
    id: window
    width: 1720
    height: 1040
    minimumWidth: 1180
    minimumHeight: 720
    visible: true
    title: "CW Buddy — " + appSettings.profileName
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

    // A VFO-style readout deserves the same decimal/centesimal precision a
    // real rig's display has (e.g. 7016.45 kHz on 40 m), which the coarser
    // formatFrequency() above deliberately does not provide for spectrum
    // axis labels.
    function formatVfoFrequency(hz) {
        if (Math.abs(hz) >= 30000000)
            return (hz / 1000000).toFixed(5) + " MHz"
        return (hz / 1000).toFixed(2) + " kHz"
    }

    function formatRigFrequency(hz) {
        var digits = Math.max(0, Math.round(hz)).toString()
        var grouped = []
        while (digits.length > 3) {
            grouped.unshift(digits.slice(-3))
            digits = digits.slice(0, -3)
        }
        grouped.unshift(digits)
        return grouped.join(".")
    }

    function formatVfoInput(hz, unitHz) {
        var decimals = unitHz === 1000000 ? 6 : 3
        return (hz / unitHz).toFixed(decimals)
                .replace(/0+$/, "").replace(/\.$/, "")
    }

    // Maps a spectrum frequency to its horizontal pixel position within the
    // spectrumDisplay item, shared by the CW guide axis marker and the
    // verified-signal area highlights so both stay pixel-aligned.
    function hzToX(hz) {
        return spectrumDisplay.x
               + (hz - spectrumDisplay.lowerFrequencyHz)
                 / (spectrumDisplay.upperFrequencyHz
                    - spectrumDisplay.lowerFrequencyHz)
                 * spectrumDisplay.width
    }

    function verificationDiagnosticsSummary(diagnostics) {
        if (!diagnostics || typeof diagnostics.candidateTracks === "undefined")
            return ""
        var parts = []
        if (diagnostics.candidateTracks > 0)
            parts.push(diagnostics.candidateTracks + " candidate")
        if (diagnostics.morseLikelyTracks > 0)
            parts.push(diagnostics.morseLikelyTracks + " morse-likely")
        if (parts.length === 0)
            return "No pre-verification candidates right now."
        var reasons = diagnostics.reasonCounts || {}
        var reasonParts = []
        for (var key in reasons) {
            if (key === "verified" || key === "signal-lost")
                continue
            reasonParts.push(key + " " + reasons[key])
        }
        var text = parts.join(", ") + " not yet verified"
        if (reasonParts.length > 0)
            text += "  •  " + reasonParts.join(", ")
        return text
    }

    function exactCallCount(text, callsign) {
        var wanted = String(callsign).trim().toUpperCase()
        if (wanted.length === 0)
            return 0
        var tokens = String(text).toUpperCase().match(/[A-Z0-9/]+/g) || []
        var count = 0
        for (var index = 0; index < tokens.length; ++index) {
            if (tokens[index] === wanted)
                ++count
        }
        return count
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
                text: "CW Buddy"
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
                    text: transmitController.onAir ? "TX ON AIR"
                          : transmitController.armed ? "TX ARMED"
                          : "TX DISARMED"
                    color: transmitController.onAir ? "#ff6b6b" : "#f3bd55"
                    font.pixelSize: 11
                    font.weight: Font.Bold
                }
            }
            ToolButton {
                text: "Profiles"
                onClicked: profileChooser.open()
                ToolTip.visible: hovered
                ToolTip.text: "Create or switch station profiles"
            }
            ToolButton {
                text: "Settings"
                onClicked: settingsDrawer.open()
                ToolTip.visible: hovered
                ToolTip.text: "Configure audio, decoder, radio, display, and station identity"
            }
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
                        onClicked: {
                            if (modelData === "QSO")
                                txDrawer.open()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: modelData === "RX"
                            ? "Current receiver workspace"
                            : modelData === "QSO"
                              ? "Open guarded transmit and QSO controls"
                              : modelData + " workspace is not available yet"
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
                id: receiverToolbar
                Layout.fillWidth: true
                z: 20
                Label { text: "Receiver workspace"; font.pixelSize: 18; font.weight: Font.DemiBold }
                ComboBox {
                    model: ["Live audio", "WAV replay"]
                    currentIndex: replayController.sourceMode
                    onActivated: replayController.sourceMode = currentIndex
                    ToolTip.visible: hovered
                    ToolTip.text: "Choose live receiver audio or a recorded WAV replay"
                }
                Rectangle { width: 1; height: 28; color: "#303a46" }
                Label { text: "Monitor"; color: "#91a0b1"; font.pixelSize: 11 }
                ToolButton {
                    objectName: "monitorOffButton"
                    text: "OFF"
                    checkable: true
                    checked: replayController.monitorMode === 0
                    onClicked: replayController.setMonitorMode(0)
                    ToolTip.visible: hovered
                    ToolTip.text: "Mute receiver monitoring"
                }
                ToolButton {
                    objectName: "monitorReceiverButton"
                    text: "RX"
                    checkable: true
                    checked: replayController.monitorMode === 1
                    enabled: replayController.activeSource
                    onClicked: replayController.setMonitorMode(1)
                    ToolTip.visible: hovered
                    ToolTip.text: "Monitor the complete receiver audio window"
                }
                ToolButton {
                    objectName: "monitorSignalButton"
                    text: "SIGNAL"
                    checkable: true
                    checked: replayController.monitorMode === 2
                    enabled: replayController.activeSource
                    onClicked: replayController.setMonitorMode(2)
                    ToolTip.visible: hovered
                    ToolTip.text: replayController.monitorStatus
                }
                Slider {
                    objectName: "monitorLevelSlider"
                    from: 0
                    to: 1
                    value: replayController.monitorLevel
                    enabled: replayController.monitorMode !== 0
                    Layout.preferredWidth: 82
                    onMoved: replayController.setMonitorLevel(value)
                    ToolTip.visible: hovered
                    ToolTip.text: "Monitor level " + Math.round(value * 100) + "%"
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
                    z: 21
                    text: "Start live RX"
                    visible: replayController.sourceMode === 0
                    enabled: !replayController.liveCapturing
                    onClicked: replayController.startLiveAudio()
                    ToolTip.visible: hovered
                    ToolTip.text: enabled
                        ? "Start spectrum analysis and CW decoding from the selected audio input"
                        : "Live receiver processing is already running"
                }
                Button {
                    text: "Stop live RX"
                    visible: replayController.sourceMode === 0
                    enabled: replayController.liveCapturing
                    onClicked: replayController.stopLiveAudio()
                    ToolTip.visible: hovered
                    ToolTip.text: enabled
                        ? "Stop live audio processing"
                        : "Live receiver processing is not running"
                }
                Button {
                    text: "Open WAV"
                    visible: replayController.sourceMode === 1
                    onClicked: wavDialog.open()
                    ToolTip.visible: hovered
                    ToolTip.text: "Choose a PCM or 32-bit float receiver recording"
                }
                Button {
                    text: replayController.playing ? "Pause" : "Play"
                    visible: replayController.sourceMode === 1
                    enabled: replayController.sourceLoaded
                    onClicked: replayController.playing ? replayController.pause() : replayController.play()
                    ToolTip.visible: hovered
                    ToolTip.text: replayController.playing
                        ? "Pause replay at the current position"
                        : "Continue decoding the selected recording"
                }
                Button {
                    text: "Stop"
                    visible: replayController.sourceMode === 1
                    enabled: replayController.sourceLoaded
                    onClicked: replayController.stop()
                    ToolTip.visible: hovered
                    ToolTip.text: "Stop replay and return to its beginning"
                }
            }

            Rectangle {
                id: spectrumPanel
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: "#09111a"
                border.color: "#263241"
                clip: true
                z: 0

                SpectrumWaterfall {
                    id: spectrumDisplay
                    objectName: "spectrumDisplay"
                    anchors.fill: parent
                    anchors.margins: 10
                    source: replayController
                    targetFps: appSettings.targetFps
                    waterfallRate: appSettings.waterfallRate
                    waterfallTimeSpanSeconds: appSettings.waterfallTimeSpanSeconds
                    displayMode: appSettings.spectrumDisplayMode
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

                Item {
                    id: cwGuideBoundaryOverlay
                    objectName: "cwGuideBoundaryOverlay"
                    // Two dashed boundaries describe the configured receive
                    // region without filling it or looking like a detected
                    // stream. Their separation is exactly the configured CW
                    // guide width around the configured center frequency.
                    property real guideLowerHz: appSettings.cwGuideCenterHz
                                                - 0.5 * appSettings.cwGuideWidthHz
                    property real guideUpperHz: appSettings.cwGuideCenterHz
                                                + 0.5 * appSettings.cwGuideWidthHz
                    readonly property color guideColor: "#ff7b84"
                    visible: appSettings.showCwGuide
                             && spectrumDisplay.upperFrequencyHz
                                > spectrumDisplay.lowerFrequencyHz
                    anchors.fill: spectrumDisplay
                    z: 3

                    Repeater {
                        model: [cwGuideBoundaryOverlay.guideLowerHz,
                                cwGuideBoundaryOverlay.guideUpperHz]
                        delegate: Item {
                            required property var modelData
                            property real boundaryHz: Number(modelData)
                            visible: boundaryHz >= spectrumDisplay.lowerFrequencyHz
                                     && boundaryHz <= spectrumDisplay.upperFrequencyHz
                            x: window.hzToX(boundaryHz) - spectrumDisplay.x - 1
                            width: 2
                            height: cwGuideBoundaryOverlay.height

                            Repeater {
                                model: Math.ceil(parent.height / 12)
                                delegate: Rectangle {
                                    required property int index
                                    y: index * 12
                                    width: 2
                                    height: 6
                                    color: cwGuideBoundaryOverlay.guideColor
                                }
                            }
                        }
                    }
                }
                MouseArea {
                    id: manualSliceHitArea
                    objectName: "manualSliceHitArea"
                    x: spectrumDisplay.x
                    y: spectrumDisplay.y
                    width: spectrumDisplay.width
                    height: spectrumDisplay.height
                    z: 6
                    enabled: replayController.activeSource
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    function frequencyAtX(positionX) {
                        var fraction = Math.max(0, Math.min(1,
                                                           positionX / width))
                        return spectrumDisplay.lowerFrequencyHz
                                + fraction
                                  * (spectrumDisplay.upperFrequencyHz
                                     - spectrumDisplay.lowerFrequencyHz)
                    }
                    function streamIdAtX(positionX) {
                        if (width <= 0 || spectrumDisplay.upperFrequencyHz
                                <= spectrumDisplay.lowerFrequencyHz) {
                            return 0
                        }
                        var pointedHz = frequencyAtX(positionX)
                        var pixelToleranceHz = 14
                                * (spectrumDisplay.upperFrequencyHz
                                   - spectrumDisplay.lowerFrequencyHz) / width
                        var toleranceHz = Math.max(60, pixelToleranceHz)
                        var nearestDistance = toleranceHz
                        var nearestId = 0
                        for (var index = 0;
                             index < replayController.decoderChannels.length;
                             ++index) {
                            var channel = replayController.decoderChannels[index]
                            if (!channel.verifiedCw
                                    && !channel.operatorSelected) {
                                continue
                            }
                            var distance = Math.abs(
                                        channel.presentationFrequencyHz
                                        - pointedHz)
                            if (distance <= nearestDistance) {
                                nearestDistance = distance
                                nearestId = channel.id
                            }
                        }
                        return nearestId
                    }
                    property var hoveredStreamId: containsMouse
                                                  ? streamIdAtX(mouseX) : 0
                    cursorShape: hoveredStreamId !== 0
                                 ? Qt.PointingHandCursor : Qt.CrossCursor
                    onClicked: function(mouse) {
                        if (!replayController.activeSource || width <= 0
                                || spectrumDisplay.upperFrequencyHz
                                   <= spectrumDisplay.lowerFrequencyHz) {
                            return
                        }
                        if (mouse.button === Qt.LeftButton) {
                            var streamId = streamIdAtX(mouse.x)
                            if (streamId !== 0)
                                replayController.openDecoderSession(streamId)
                            return
                        }
                        var frequencyHz = frequencyAtX(mouse.x)
                        appSettings.cwGuideCenterHz = frequencyHz
                        replayController.openManualDecoderSession(frequencyHz)
                    }
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 350
                    ToolTip.text: hoveredStreamId !== 0
                        ? (replayController.monitorMode === 2
                           ? "Left click: open and monitor this decoded stream\n"
                           : "Left click: open this decoded stream\n")
                          + "Right click: move the guide and start a manual decoder probe\n"
                          + "Ctrl+click: TX-frequency selection is not available until the linked provider supports guarded TX-VFO writes"
                        : "Left click: no decoded stream at this position\n"
                          + "Right click: move the guide and start a manual decoder probe\n"
                          + "Ctrl+click: TX-frequency selection is not available until the linked provider supports guarded TX-VFO writes"
                }
                Rectangle {
                    objectName: "spectrumPointerHelp"
                    visible: manualSliceHitArea.containsMouse
                    z: 9
                    anchors.left: spectrumDisplay.left
                    anchors.bottom: spectrumDisplay.bottom
                    anchors.leftMargin: 12
                    anchors.bottomMargin: 12
                    width: Math.min(pointerHelpText.implicitWidth + 22,
                                    spectrumDisplay.width - 24)
                    height: pointerHelpText.implicitHeight + 14
                    radius: 5
                    color: "#dd111720"
                    border.color: "#526172"
                    border.width: 1
                    Label {
                        id: pointerHelpText
                        anchors.centerIn: parent
                        text: manualSliceHitArea.hoveredStreamId !== 0
                              ? "LEFT: open stream   •   RIGHT: manual probe   •   CTRL: TX VFO unavailable"
                              : "LEFT: no stream   •   RIGHT: manual probe   •   CTRL: TX VFO unavailable"
                        color: "#d4dbe4"
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                }
                ToolButton {
                    id: tuneRxDownButton
                    objectName: "tuneRxDownButton"
                    visible: replayController.sourceMode === 0
                             && replayController.radioFrequencyAvailable
                             && appSettings.radioFrequencyWritable
                    anchors.left: spectrumDisplay.left
                    y: spectrumDisplay.y + spectrumDisplay.height * 0.68
                       - height / 2
                    width: 38
                    height: 58
                    z: 8
                    text: "<"
                    font.pixelSize: 24
                    Accessible.name: "Tune RX down"
                    Accessible.description: "Decrease the receive frequency by "
                                            + (appSettings.radioTuningStepHz / 1000)
                                            + " kilohertz"
                    onClicked: appSettings.stepControlledRxFrequency(-1)
                    ToolTip.visible: hovered
                    ToolTip.delay: 250
                    ToolTip.text: "Tune RX down "
                                  + (appSettings.radioTuningStepHz / 1000)
                                  + " kHz. Split TX and mode stay unchanged."
                }
                ToolButton {
                    id: tuneRxUpButton
                    objectName: "tuneRxUpButton"
                    visible: replayController.sourceMode === 0
                             && replayController.radioFrequencyAvailable
                             && appSettings.radioFrequencyWritable
                    anchors.right: spectrumDisplay.right
                    y: spectrumDisplay.y + spectrumDisplay.height * 0.68
                       - height / 2
                    width: 38
                    height: 58
                    z: 8
                    text: ">"
                    font.pixelSize: 24
                    Accessible.name: "Tune RX up"
                    Accessible.description: "Increase the receive frequency by "
                                            + (appSettings.radioTuningStepHz / 1000)
                                            + " kilohertz"
                    onClicked: appSettings.stepControlledRxFrequency(1)
                    ToolTip.visible: hovered
                    ToolTip.delay: 250
                    ToolTip.text: "Tune RX up "
                                  + (appSettings.radioTuningStepHz / 1000)
                                  + " kHz. Split TX and mode stay unchanged."
                }
                Repeater {
                    model: replayController.decoderChannels
                    delegate: Item {
                        id: channelMarker
                        required property var modelData
                        required property int index
                        property real channelHz: modelData.presentationFrequencyHz
                        // Keep presentation geometry independent of the
                        // decoder's adaptive 60/120/240 Hz analysis filter.
                        // That filter may legitimately change while decoding,
                        // but making the clickable marker follow it causes a
                        // distracting size flicker and falsely suggests that
                        // the transmitted carrier itself is changing width.
                        readonly property real markerWidthHz: 120
                        property real areaWidthPx: Math.max(28,
                            window.hzToX(channelHz + markerWidthHz / 2)
                            - window.hzToX(channelHz - markerWidthHz / 2))
                        visible: (modelData.verifiedCw
                                  || modelData.operatorSelected)
                                 && channelHz >= spectrumDisplay.lowerFrequencyHz
                                 && channelHz <= spectrumDisplay.upperFrequencyHz
                                 && spectrumDisplay.upperFrequencyHz
                                    > spectrumDisplay.lowerFrequencyHz
                        x: window.hzToX(channelHz) - width / 2
                        y: spectrumDisplay.y
                        width: areaWidthPx
                        height: spectrumDisplay.height
                        z: 5
                        property bool pointerHovered:
                            manualSliceHitArea.hoveredStreamId === modelData.id
                        Rectangle {
                            anchors.fill: parent
                            color: modelData.color
                            opacity: modelData.verifiedCw
                                     ? (modelData.active ? 0.28 : 0.0)
                                     : 0.10
                            border.color: modelData.color
                            border.width: modelData.verifiedCw
                                          ? (modelData.active ? 1 : 0) : 1
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: modelData.keyDown ? 3 : 1
                            height: parent.height
                            color: modelData.color
                            visible: modelData.verifiedCw && modelData.active
                            opacity: 0.9
                        }
                        Rectangle {
                            // A retained but inactive stream leaves the plot
                            // clear and keeps only a short identity-colored
                            // mark on the frequency axis.
                            visible: modelData.verifiedCw && !modelData.active
                            y: parent.height * 0.36 - 1
                            width: parent.width
                            height: 3
                            color: modelData.color
                            opacity: 0.9
                        }
                        Label {
                            anchors.left: parent.horizontalCenter
                            anchors.leftMargin: 7
                            y: spectrumDisplay.height * 0.36 - height - 12
                            transformOrigin: Item.BottomLeft
                            rotation: -90
                            text: modelData.callingOwnStation
                                  ? "\u25CF CALLING YOU"
                                  : !modelData.verifiedCw
                                  ? modelData.frequencyLabel + " • manual"
                                  : modelData.callsign.length > 0
                                  ? (modelData.callsign
                                     + (modelData.callsignInDatabase ? " \u2713" : ""))
                                  : modelData.callsignSuggestion.length > 0
                                  ? "≈ " + modelData.callsignSuggestion
                                  : modelData.frequencyLabel
                            color: modelData.color
                            font.pixelSize: channelMarker.pointerHovered ? 32 : 18
                            font.weight: Font.Bold
                            leftPadding: 4
                            rightPadding: 4
                            topPadding: 2
                            bottomPadding: 2
                            z: 2
                            background: Rectangle {
                                id: channelLabelBackground
                                objectName: "channelLabelBackground"
                                // A callsign corroborated by the operator's
                                // offline list is drawn as a solid chip. One
                                // that was only decoded keeps the plain
                                // background, because an unlisted station is
                                // ordinary rather than suspect. A stream that
                                // is calling the operator overrides both: it
                                // is the one thing here the operator must not
                                // miss, and it has to be visible on the
                                // spectrum, before any card is opened.
                                color: modelData.callingOwnStation
                                       ? "#5a1420"
                                       : (modelData.callsignInDatabase
                                          ? "#16241a" : "#e6091018")
                                border.color: modelData.callingOwnStation
                                              ? "#ff6b6b" : modelData.color
                                border.width: modelData.callingOwnStation
                                              ? 2
                                              : (modelData.callsignInDatabase
                                                 || channelMarker.pointerHovered ? 1 : 0)
                                radius: 3
                                SequentialAnimation on opacity {
                                    running: modelData.callingOwnStation
                                    loops: Animation.Infinite
                                    alwaysRunToEnd: true
                                    NumberAnimation { from: 1.0; to: 0.35; duration: 420 }
                                    // The cycle ends fully opaque and is
                                    // allowed to finish, so the marker never
                                    // freezes half faded when the call stops.
                                    NumberAnimation { from: 0.35; to: 1.0; duration: 420 }
                                }
                            }
                            Behavior on font.pixelSize {
                                NumberAnimation { duration: 90 }
                            }
                        }
                        ToolTip.visible: channelMarker.pointerHovered
                        ToolTip.delay: 450
                        ToolTip.text: (modelData.callsign.length > 0
                                       ? modelData.callsign + "\n"
                                       : (modelData.callsignSuggestion.length > 0
                                          ? (modelData.callsignSuggestionSource
                                             === "offline-directory"
                                             ? "Offline suggestion: "
                                             : "Acoustic suggestion: ")
                                            + modelData.callsignSuggestion
                                            + " from "
                                            + modelData.callsignSuggestionRawSpan
                                            + "\n" : ""))
                                      + modelData.frequencyLabel + "\n"
                                      + (!modelData.verifiedCw
                                         ? "Manual slice • awaiting ordinary CW verification\n"
                                         : "")
                                      + modelData.audioFrequencyHz.toFixed(1)
                                        + " Hz audio • "
                                      + modelData.filterWidthHz.toFixed(0)
                                        + " Hz filter • "
                                      + modelData.driftHzPerSecond.toFixed(1)
                                        + " Hz/s drift"
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
                        objectName: "emptyStateStartButton"
                        Layout.alignment: Qt.AlignHCenter
                        text: replayController.sourceMode === 0 ? "Start live RX" : "Choose WAV recording"
                        onClicked: replayController.sourceMode === 0 ? replayController.startLiveAudio() : wavDialog.open()
                        ToolTip.visible: hovered
                        ToolTip.text: replayController.sourceMode === 0
                            ? "Start spectrum analysis and CW decoding"
                            : "Choose a receiver WAV recording"
                    }
                }
            }

            Frame {
                id: liveControlsFrame
                objectName: "liveControlsFrame"
                property bool pinned: false
                readonly property bool expanded: pinned || controlsHover.hovered
                                                 || viewSelector.popup.visible
                Layout.fillWidth: true
                Layout.preferredHeight: expanded
                                        ? controlsContent.implicitHeight + 16
                                        : controlsHeader.implicitHeight + 16
                padding: 8
                clip: true
                background: Rectangle {
                    radius: 8
                    color: "#151b23"
                    border.color: "#2b3541"
                }
                ColumnLayout {
                    id: controlsContent
                    width: parent.width
                    spacing: 4
                    RowLayout {
                        id: controlsHeader
                        Layout.fillWidth: true
                        Label {
                            text: liveControlsFrame.expanded
                                  ? "Live spectrum controls"
                                  : "Live spectrum controls — hover to open"
                            font.weight: Font.DemiBold
                        }
                        TabBar {
                            id: liveControlTabs
                            Layout.preferredWidth: 190
                            TabButton { text: "Signal" }
                            TabButton { text: "Display" }
                        }
                        Item { Layout.fillWidth: true }
                        Button {
                            text: "Save profile"
                            onClicked: appSettings.apply()
                            ToolTip.visible: hovered
                            ToolTip.text: "Save the current live spectrum controls in this profile"
                        }
                        ToolButton {
                            objectName: "pinLiveControlsButton"
                            text: liveControlsFrame.pinned ? "Unpin" : "Pin"
                            checkable: true
                            checked: liveControlsFrame.pinned
                            onToggled: liveControlsFrame.pinned = checked
                            ToolTip.visible: hovered
                            ToolTip.text: checked
                                          ? "Restore automatic hiding"
                                          : "Keep controls open"
                        }
                    }
                    StackLayout {
                        Layout.fillWidth: true
                        enabled: liveControlsFrame.expanded
                        opacity: liveControlsFrame.expanded ? 1.0 : 0.0
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
                        GridLayout {
                            id: displayControls
                            Layout.fillWidth: true
                            columns: width >= 1200 ? 6 : 4
                            columnSpacing: 14
                            rowSpacing: 4

                            ColumnLayout {
                                Label { text: "View"; font.pixelSize: 11 }
                                ComboBox {
                                    id: viewSelector
                                    objectName: "viewSelector"
                                    Layout.preferredWidth: 150
                                    model: ["Audio spectrum", "CW symbols"]
                                    currentIndex: appSettings.spectrumDisplayMode
                                    onActivated: appSettings.spectrumDisplayMode = currentIndex
                                }
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "FPS"
                                from: 10; to: 120; stepSize: 1
                                value: appSettings.targetFps
                                onMoved: value => appSettings.targetFps = Math.round(value)
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "Lines / second"
                                from: 1; to: 120; stepSize: 1
                                value: appSettings.waterfallRate
                                onMoved: value => appSettings.waterfallRate = Math.round(value)
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "Averaging"
                                from: 1; to: 32; stepSize: 1
                                value: appSettings.averagingFrames
                                onMoved: value => appSettings.averagingFrames = Math.round(value)
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "History (seconds)"
                                from: 5; to: 30; stepSize: 1
                                value: appSettings.waterfallTimeSpanSeconds
                                onMoved: value => appSettings.waterfallTimeSpanSeconds = Math.round(value)
                            }
                            CheckBox {
                                objectName: "liveAutomaticLevelsCheck"
                                text: "Auto levels"
                                checked: appSettings.automaticRange
                                onToggled: appSettings.automaticRange = checked
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                visible: appSettings.automaticRange
                                caption: "Automatic span (dB)"
                                from: 30; to: 100; stepSize: 1
                                value: appSettings.automaticRangeSpanDb
                                onMoved: value => appSettings.automaticRangeSpanDb = value
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                visible: !appSettings.automaticRange
                                caption: "Floor (dBFS)"
                                from: -200; to: 40; stepSize: 1
                                value: appSettings.lowerBoundDb
                                onMoved: value => appSettings.lowerBoundDb = value
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                visible: !appSettings.automaticRange
                                caption: "Ceiling (dBFS)"
                                from: -190; to: 50; stepSize: 1
                                value: appSettings.upperBoundDb
                                onMoved: value => appSettings.upperBoundDb = value
                            }
                            CheckBox {
                                objectName: "liveCwGuideCheck"
                                text: "CW boundaries"
                                checked: appSettings.showCwGuide
                                onToggled: appSettings.showCwGuide = checked
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "CW center (Hz)"
                                from: 0
                                to: Math.max(3000, spectrumDisplay.upperFrequencyHz)
                                stepSize: 10
                                value: appSettings.cwGuideCenterHz
                                enabled: appSettings.showCwGuide
                                onMoved: value => appSettings.cwGuideCenterHz = value
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "CW width (Hz)"
                                from: 10; to: 5000; stepSize: 10
                                value: appSettings.cwGuideWidthHz
                                enabled: appSettings.showCwGuide
                                onMoved: value => appSettings.cwGuideWidthHz = value
                            }
                            CheckBox {
                                objectName: "liveNoiseSuppressionCheck"
                                text: "Suppress audio noise"
                                checked: appSettings.waterfallNoiseSuppression
                                enabled: appSettings.spectrumDisplayMode === 0
                                onToggled: appSettings.waterfallNoiseSuppression = checked
                            }
                            LabeledSlider {
                                Layout.fillWidth: true
                                caption: "Audio margin (dB)"
                                from: 0; to: 30; stepSize: 1
                                value: appSettings.waterfallNoiseMarginDb
                                enabled: appSettings.spectrumDisplayMode === 0
                                         && appSettings.waterfallNoiseSuppression
                                onMoved: value => appSettings.waterfallNoiseMarginDb = value
                            }
                            Label {
                                text: "Noise " + spectrumDisplay.estimatedNoiseFloorDb.toFixed(0)
                                      + " dBFS"
                                color: "#8290a0"
                            }
                        }
                    }
                }
                HoverHandler { id: controlsHover }
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
            Layout.preferredWidth: 470
            Layout.minimumWidth: 430
            Layout.fillHeight: true
            color: "#111720"
            border.color: "#263241"
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "CW Decoder"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        id: diagnosticsToggle
                        objectName: "diagnosticsToggle"
                        text: checked ? "Hide diagnostics" : "Diagnostics"
                        checkable: true
                        font.pixelSize: 10
                        ToolTip.visible: hovered
                        ToolTip.text: checked
                            ? "Hide decoder verification diagnostics"
                            : "Show why tracks are accepted or rejected"
                    }
                    ToolButton {
                        objectName: "debugCaptureButton"
                        text: replayController.debugCaptureActive ? "Stop capture" : "Debug capture"
                        font.pixelSize: 10
                        enabled: replayController.debugCaptureActive || replayController.liveCapturing
                        onClicked: replayController.debugCaptureActive
                                   ? replayController.stopDebugCapture()
                                   : replayController.startDebugCapture()
                        ToolTip.visible: hovered
                        ToolTip.delay: 300
                        ToolTip.text: "Records raw live audio and per-track decoder internals to disk for troubleshooting a signal that will not decode. Stops itself after the limit set in Settings → Decoder. Review the saved files before sharing them — the audio is whatever the selected input picked up."
                    }
                }
                Rectangle {
                    id: vfoDisplay
                    objectName: "vfoDisplay"
                    // The VFO readout only means anything with a live,
                    // connected radio driving the audio (CAT/OmniRig); it is
                    // hidden entirely for receive-only SWL setups and WAV
                    // replay, where there is no radio state to show.
                    visible: replayController.radioFrequencyAvailable
                             && replayController.sourceMode === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 126
                    radius: 5
                    color: "#20262e"
                    border.color: "#59636f"
                    border.width: 1
                    clip: true

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 6
                        Rectangle {
                            id: onAirIndicator
                            objectName: "onAirIndicator"
                            // Bound only to guarded local KEY state. Preparing
                            // text, decoder suggestions, CAT state, and merely
                            // arming TX can never illuminate this indicator.
                            // It remains dim until the tested hardware adapter
                            // is implemented and the guard reports KEY down.
                            property bool active: transmitController.onAir
                            Layout.preferredWidth: 68
                            Layout.fillHeight: true
                            radius: 4
                            color: active ? "#4d0d18" : "#252b33"
                            border.color: active ? "#ff3b30" : "#3a4552"
                            border.width: 1
                            Image {
                                anchors.centerIn: parent
                                source: "qrc:/icons/on-air-active.png"
                                width: 58
                                height: 58
                                fillMode: Image.PreserveAspectFit
                                opacity: onAirIndicator.active ? 1.0 : 0.24
                            }
                            ToolTip.visible: onAirMouse.containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: active
                                ? "KEY is authoritatively asserted"
                                : "Not transmitting"
                            MouseArea {
                                id: onAirMouse
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 5

                            Item {
                                id: vfoRxEditor
                                objectName: "vfoRxEditor"
                                property bool editing: false
                                property bool invalidEntry: false
                                property int inputUnitHz: 1000
                                property string inputUnitLabel: inputUnitHz === 1000000
                                                                ? "MHz" : "kHz"
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                function beginEdit() {
                                    if (!appSettings.radioFrequencyWritable)
                                        return
                                    inputUnitHz = replayController.radioRxFrequencyHz
                                                  >= 30000000 ? 1000000 : 1000
                                    vfoRxFrequencyField.text = window.formatVfoInput(
                                                replayController.radioRxFrequencyHz,
                                                inputUnitHz)
                                    invalidEntry = false
                                    editing = true
                                    vfoRxFrequencyField.forceActiveFocus()
                                    vfoRxFrequencyField.selectAll()
                                }
                                function dismissEdit() {
                                    invalidEntry = false
                                    editing = false
                                }
                                function cancelEdit() {
                                    dismissEdit()
                                    window.contentItem.forceActiveFocus()
                                }
                                function acceptEdit() {
                                    if (appSettings.setControlledRxFrequency(
                                                vfoRxFrequencyField.text,
                                                inputUnitHz)) {
                                        invalidEntry = false
                                        editing = false
                                        window.contentItem.forceActiveFocus()
                                    } else {
                                        invalidEntry = true
                                        vfoRxFrequencyField.forceActiveFocus()
                                        vfoRxFrequencyField.selectAll()
                                    }
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: 3
                                    color: "#080c10"
                                    border.color: vfoRxEditor.invalidEntry
                                                  ? "#ff7b84" : "#46515e"
                                    border.width: vfoRxEditor.editing ? 2 : 1
                                }
                                Label {
                                    id: vfoRxLabel
                                    objectName: "vfoRxLabel"
                                    anchors.left: parent.left
                                    anchors.leftMargin: 12
                                    anchors.right: parent.right
                                    anchors.rightMargin: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: !vfoRxEditor.editing
                                    text: window.formatRigFrequency(
                                              replayController.radioRxFrequencyHz)
                                    color: "#f5f8fb"
                                    font.family: "monospace"
                                    font.pixelSize: 34
                                    font.weight: Font.Bold
                                    font.letterSpacing: 2
                                    horizontalAlignment: Text.AlignRight
                                    elide: Text.ElideLeft
                                }
                                Label {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 10
                                    anchors.top: parent.top
                                    anchors.topMargin: 6
                                    visible: !vfoRxEditor.editing
                                    text: "RX"
                                    color: "#64dff0"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                                MouseArea {
                                    id: vfoRxEditHitArea
                                    objectName: "vfoRxEditHitArea"
                                    anchors.fill: parent
                                    visible: !vfoRxEditor.editing
                                    enabled: appSettings.radioFrequencyWritable
                                    hoverEnabled: true
                                    activeFocusOnTab: enabled
                                    cursorShape: enabled ? Qt.IBeamCursor
                                                         : Qt.ArrowCursor
                                    Accessible.role: Accessible.Button
                                    Accessible.name: "Edit RX frequency"
                                    Accessible.description: "Enter an exact receive frequency"
                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Return
                                                || event.key === Qt.Key_Enter
                                                || event.key === Qt.Key_Space) {
                                            vfoRxEditor.beginEdit()
                                            event.accepted = true
                                        }
                                    }
                                    onClicked: {
                                        forceActiveFocus()
                                        vfoRxEditor.beginEdit()
                                    }
                                }
                                RowLayout {
                                    id: vfoRxEditRow
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 8
                                    visible: vfoRxEditor.editing
                                    spacing: 6
                                    Label {
                                        text: "RX"
                                        color: "#64dff0"
                                        font.family: "monospace"
                                        font.pixelSize: 18
                                        font.weight: Font.Bold
                                    }
                                    TextField {
                                        id: vfoRxFrequencyField
                                        objectName: "vfoRxFrequencyField"
                                        Layout.fillWidth: true
                                        selectByMouse: true
                                        color: vfoRxEditor.invalidEntry
                                               ? "#ff7b84" : "#f5f8fb"
                                        selectionColor: "#2dd4a7"
                                        selectedTextColor: "#03100b"
                                        font.family: "monospace"
                                        font.pixelSize: 22
                                        font.weight: Font.Bold
                                        font.letterSpacing: 2
                                        background: Rectangle {
                                            radius: 3
                                            color: "#02070a"
                                            border.color: vfoRxEditor.invalidEntry
                                                          ? "#ff7b84" : "#647180"
                                            border.width: 1
                                        }
                                        Keys.onPressed: function(event) {
                                            if (event.key === Qt.Key_Escape) {
                                                vfoRxEditor.cancelEdit()
                                                event.accepted = true
                                            } else if (event.key === Qt.Key_Return
                                                       || event.key === Qt.Key_Enter) {
                                                vfoRxEditor.acceptEdit()
                                                event.accepted = true
                                            }
                                        }
                                        onActiveFocusChanged: {
                                            if (!activeFocus && vfoRxEditor.editing)
                                                vfoRxEditor.dismissEdit()
                                        }
                                    }
                                    Label {
                                        text: vfoRxEditor.inputUnitLabel
                                        color: "#91a0b1"
                                        font.family: "monospace"
                                        font.pixelSize: 13
                                    }
                                }
                                ToolTip.visible: vfoRxEditHitArea.containsMouse
                                ToolTip.delay: 300
                                ToolTip.text: appSettings.radioFrequencyWritable
                                    ? "Click to enter an exact RX frequency"
                                    : "The linked provider reports frequency read-only"
                                Connections {
                                    target: appSettings
                                    function onRadioFrequencyControlChanged() {
                                        if (!appSettings.radioFrequencyWritable) {
                                            vfoRxEditor.invalidEntry = false
                                            vfoRxEditor.editing = false
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 38
                                spacing: 5
                                Rectangle {
                                    objectName: "vfoSplitBadge"
                                    Layout.preferredWidth: 68
                                    Layout.fillHeight: true
                                    radius: 2
                                    color: replayController.radioSplitActive
                                           ? "#f0f21c" : "#353b43"
                                    Label {
                                        id: splitBadgeLabel
                                        anchors.centerIn: parent
                                        text: replayController.radioSplitActive
                                              ? "SPLIT" : "SIMPLEX"
                                        color: replayController.radioSplitActive
                                               ? "#111318" : "#7b8794"
                                        font.pixelSize: 11
                                        font.weight: Font.Bold
                                    }
                                }
                                Rectangle {
                                    objectName: "vfoRxModeBadge"
                                    Layout.preferredWidth: 58
                                    Layout.fillHeight: true
                                    radius: 2
                                    color: "#f0f21c"
                                    Label {
                                        anchors.centerIn: parent
                                        text: appSettings.cwToneSidebandIndex === 0
                                              ? "CW" : "CW-R"
                                        color: "#111318"
                                        font.pixelSize: 13
                                        font.weight: Font.Bold
                                    }
                                    ToolTip.visible: rxModeMouse.containsMouse
                                    ToolTip.text: "Mode display; provider-neutral mode control is not available yet"
                                    MouseArea {
                                        id: rxModeMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 2
                                    color: "#15110c"
                                    Label {
                                        objectName: "vfoTxLabel"
                                        anchors.left: parent.left
                                        anchors.leftMargin: 8
                                        anchors.right: parent.right
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "TX  " + window.formatRigFrequency(
                                                  replayController.radioTxFrequencyHz > 0
                                                  ? replayController.radioTxFrequencyHz
                                                  : replayController.radioRxFrequencyHz)
                                        color: replayController.radioSplitActive
                                               ? "#ff6a24" : "#9b694e"
                                        font.family: "monospace"
                                        font.pixelSize: 18
                                        font.weight: Font.Bold
                                        font.letterSpacing: 1
                                        horizontalAlignment: Text.AlignRight
                                        elide: Text.ElideLeft
                                    }
                                    ToolTip.visible: txFrequencyMouse.containsMouse
                                    ToolTip.text: "TX frequency display; provider-neutral TX editing follows in the radio-control slice"
                                    MouseArea {
                                        id: txFrequencyMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }
                                }
                            }
                        }
                    }
                }
                Label {
                    objectName: "vfoRxEditErrorLabel"
                    visible: vfoDisplay.visible && vfoRxEditor.invalidEntry
                    text: appSettings.statusMessage
                    color: "#ff7b84"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    visible: replayController.debugCaptureActive || replayController.debugCapturePath.length > 0
                    text: replayController.debugCaptureActive
                          ? "Capturing… " + replayController.debugCaptureElapsedSeconds.toFixed(0) + "s / " + appSettings.debugCaptureMaximumSeconds + "s max — " + replayController.debugCapturePath
                          : "Last capture: " + replayController.debugCaptureNote + " — " + replayController.debugCapturePath
                    color: replayController.debugCaptureActive ? "#f3bd55" : "#6c7c8e"
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
                Label {
                    text: replayController.decoderChannelCount > 0
                          ? replayController.decoderChannelCount
                            + " signal(s) detected • "
                            + replayController.decoderSessionCount
                            + " session(s) open"
                          : replayController.decoderSessionCount > 0
                            ? "Manual slice open • awaiting CW verification"
                          : "Scanning the complete processed passband"
                    color: "#8290a0"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    id: diagnosticsSummaryLabel
                    objectName: "diagnosticsSummaryLabel"
                    property string summary: ""
                    visible: diagnosticsToggle.checked && summary.length > 0
                    text: summary
                    color: "#6c7c8e"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    Timer {
                        // Sampled at most once per second so this is a
                        // troubleshooting snapshot, not a flickering readout.
                        interval: 1000
                        running: diagnosticsToggle.checked
                        repeat: true
                        triggeredOnStart: true
                        onTriggered: diagnosticsSummaryLabel.summary =
                            window.verificationDiagnosticsSummary(
                                replayController.verificationDiagnostics)
                    }
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
                        // Lifted while being dragged so the card being moved is
                        // obvious against the ones it passes.
                        opacity: sessionDragHandler.active ? 0.85 : 1.0
                        width: decoderChannelList.width
                        // Derive the delegate height from its actual rows. A
                        // fixed card height let platform font/control metrics
                        // push the transcript, local-model status, or footer
                        // through the rounded border.
                        height: Math.ceil(sessionCardLayout.implicitHeight + 20)
                        radius: 7
                        color: "#151d27"
                        border.width: modelData.keyDown ? 2 : 1
                        border.color: modelData.color
                        clip: true
                        z: sessionDragHandler.active ? 10 : 1
                        property string rawDecodedText: modelData.text.length > 0
                            ? modelData.text
                            : (modelData.provisionalText.length > 0
                               ? modelData.provisionalText
                               : (modelData.elements.length > 0
                                  ? modelData.elements
                                  : (!modelData.verifiedCw
                                     ? "Analyzing the selected frequency…"
                                     : "Listening…")))
                        // Prefer the turn-aware presentation. It changes only
                        // conservative word boundaries and separates completed
                        // transmissions; raw and phase-consensus evidence stay
                        // available to diagnostics without modification.
                        property string displayedDecodedText:
                            modelData.contextualText.length > 0
                            ? modelData.contextualText
                            : (modelData.refinedText.length > 0
                               ? modelData.refinedText : rawDecodedText)
                        property string callsignEvidenceText:
                            rawDecodedText + " " + modelData.refinedText
                            + " " + modelData.contextualText
                        property string ownCallEvidenceText:
                            callsignEvidenceText + " " + localModelStableText
                        property string localModelState:
                            modelData.localModelState
                        property string localModelStatus:
                            modelData.localModelStatus
                        property string localModelStableText:
                            modelData.localModelText
                        property string localModelCallsign:
                            modelData.localModelCallsign
                        property string advisoryCallsignSuggestion:
                            modelData.callsignSuggestion
                        property string callsignSuggestionSource:
                            modelData.callsignSuggestionSource
                        property bool localModelHasText:
                            localModelState === "ready"
                            && localModelStableText.length > 0
                        property int ownCallMatches: window.exactCallCount(
                            ownCallEvidenceText, appSettings.ownCallsign)
                        property int previousOwnCallMatches: 0
                        onOwnCallMatchesChanged: {
                            if (ownCallMatches > previousOwnCallMatches)
                                ownCallFlashAnimation.restart()
                            previousOwnCallMatches = ownCallMatches
                        }
                        Rectangle {
                            id: ownCallFlash
                            anchors.fill: parent
                            radius: sessionCard.radius
                            color: "transparent"
                            border.color: "#ffd54f"
                            border.width: 4
                            opacity: 0
                            z: 30
                        }
                        SequentialAnimation {
                            id: ownCallFlashAnimation
                            loops: 5
                            NumberAnimation {
                                target: ownCallFlash
                                property: "opacity"
                                from: 0
                                to: 0.9
                                duration: 160
                            }
                            NumberAnimation {
                                target: ownCallFlash
                                property: "opacity"
                                from: 0.9
                                to: 0
                                duration: 240
                            }
                        }
                        ColumnLayout {
                            id: sessionCardLayout
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
                                    Layout.fillWidth: true
                                    text: {
                                        if (modelData.qsoParticipants.length >= 2) {
                                            return "QSO  "
                                                + modelData.qsoParticipants[0]
                                                + " ↔ "
                                                + modelData.qsoParticipants[1]
                                                + "  •  "
                                                + modelData.frequencyLabel
                                        }
                                        var station = modelData.callsign
                                        if (station.length === 0) {
                                            station = sessionCard.localModelCallsign
                                        }
                                        if (station.length === 0
                                                && sessionCard.advisoryCallsignSuggestion.length > 0) {
                                            station = "≈ " + sessionCard.advisoryCallsignSuggestion
                                        }
                                        return station.length > 0
                                            ? station + "  •  "
                                                + modelData.frequencyLabel
                                            : modelData.frequencyLabel
                                    }
                                    color: modelData.color
                                    font.weight: Font.Bold
                                    font.pixelSize: 16
                                    elide: Text.ElideRight
                                }
                                Button {
                                    objectName: "decoderSessionTxButton"
                                    text: "TX " + modelData.callsign
                                    visible: modelData.callsign.length > 0
                                    enabled: transmitController.armed
                                    onClicked: {
                                        transmitController.selectTarget(
                                            modelData.id, modelData.callsign,
                                            modelData.frequencyKind === "RF"
                                            ? modelData.displayFrequencyHz : 0)
                                        txDrawer.open()
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: transmitController.armed
                                        ? "Select this exactly decoded station for guarded TX"
                                        : "Open QSO and arm TX first"
                                }
                                Label {
                                    objectName: "callsignDatabaseBadge"
                                    visible: modelData.callsign.length > 0
                                             && modelData.callsignDatabaseLoaded
                                    text: modelData.callsignInDatabase
                                          ? "\u2713 LISTED" : "DECODED"
                                    color: modelData.callsignInDatabase
                                           ? "#0b1a10" : "#91a0b1"
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    leftPadding: 5
                                    rightPadding: 5
                                    topPadding: 2
                                    bottomPadding: 2
                                    background: Rectangle {
                                        radius: 3
                                        color: modelData.callsignInDatabase
                                               ? "#7fd18a" : "transparent"
                                        border.color: modelData.callsignInDatabase
                                                      ? "#7fd18a" : "#3a4756"
                                        border.width: 1
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 400
                                    ToolTip.text: modelData.callsignInDatabase
                                        ? "This callsign appears in the offline callsign list."
                                        : "Decoded from the air. It is not in the offline callsign list, which is normal for an unlisted station."
                                    property bool hovered: badgeHover.hovered
                                    HoverHandler { id: badgeHover }
                                }
                                Label {
                                    visible: modelData.callsign.length === 0
                                             && sessionCard.localModelCallsign.length > 0
                                    text: "MODEL"
                                    color: "#80cbc4"
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                }
                                Label {
                                    objectName: "advisoryCallsignSuggestionBadge"
                                    visible: modelData.callsign.length === 0
                                             && sessionCard.localModelCallsign.length === 0
                                             && sessionCard.advisoryCallsignSuggestion.length > 0
                                    text: sessionCard.callsignSuggestionSource
                                          === "offline-directory" ? "DB" : "AUDIO"
                                    color: sessionCard.callsignSuggestionSource
                                           === "offline-directory"
                                           ? "#f3bd55" : "#80cbc4"
                                    font.pixelSize: 9
                                    font.weight: Font.Bold
                                    Accessible.name: "Advisory callsign suggestion"
                                    Accessible.description: "Advisory match "
                                                            + sessionCard.advisoryCallsignSuggestion
                                                            + " for decoded span "
                                                            + modelData.callsignSuggestionRawSpan
                                    ToolTip.visible: hovered
                                    ToolTip.text: (sessionCard.callsignSuggestionSource
                                                   === "offline-directory"
                                                   ? "Advisory offline-directory match for "
                                                   : "Advisory acoustic consensus for ")
                                                  + modelData.callsignSuggestionRawSpan
                                                  + "; decoded text is unchanged"
                                }
                                Label {
                                    visible: sessionCard.ownCallMatches > 0
                                    text: "YOUR CALL HEARD"
                                    color: "#ffd54f"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                                Label {
                                    text: !modelData.verifiedCw
                                          ? "MANUAL"
                                          : (modelData.active ? "ACTIVE" : "HOLD")
                                    color: modelData.active ? modelData.color : "#718091"
                                    font.pixelSize: 10
                                }
                                ToolButton {
                                    objectName: "decoderSessionDragHandle"
                                    text: "⠿"
                                    // Cards are reordered by dragging this
                                    // handle. The keyboard path is kept for
                                    // operators who cannot drag, and for
                                    // accessibility: the same control moves the
                                    // card with the arrow keys when focused.
                                    Accessible.name: "Reorder decoded session"
                                    Accessible.description:
                                        "Drag to reposition, or use the up and down arrow keys"
                                    focusPolicy: Qt.StrongFocus
                                    ToolTip.visible: hovered
                                    ToolTip.delay: 500
                                    ToolTip.text: "Drag to reorder this card"
                                    Keys.onUpPressed: if (sessionCard.index > 0)
                                        replayController.moveDecoderSession(
                                            modelData.id, sessionCard.index - 1)
                                    Keys.onDownPressed:
                                        if (sessionCard.index + 1 < decoderChannelList.count)
                                            replayController.moveDecoderSession(
                                                modelData.id, sessionCard.index + 1)
                                    DragHandler {
                                        id: sessionDragHandler
                                        objectName: "decoderSessionDragHandler"
                                        // The list owns delegate placement, so
                                        // the card is not moved directly. The
                                        // travelled distance is converted into
                                        // a position change on release, which
                                        // keeps the list authoritative and
                                        // needs no reparenting.
                                        target: null
                                        xAxis.enabled: false
                                        yAxis.enabled: true
                                        property real pressY: 0
                                        onActiveChanged: {
                                            if (active) {
                                                pressY = centroid.scenePosition.y
                                                return
                                            }
                                            var travelled = centroid.scenePosition.y - pressY
                                            var step = Math.round(
                                                travelled / Math.max(1, sessionCard.height))
                                            if (step === 0) return
                                            var target = Math.max(0, Math.min(
                                                decoderChannelList.count - 1,
                                                sessionCard.index + step))
                                            if (target !== sessionCard.index)
                                                replayController.moveDecoderSession(
                                                    modelData.id, target)
                                        }
                                    }
                                }
                                ToolButton {
                                    objectName: "closeDecoderSessionButton"
                                    text: "×"
                                    z: 40
                                    Accessible.name: "Close decoded session"
                                    onPressed: replayController.closeDecoderSession(
                                                   modelData.id)
                                    ToolTip.visible: hovered
                                    ToolTip.text: "Close this card; decoding continues in the background"
                                }
                            }
                            Label {
                                objectName: "currentSenderLabel"
                                Layout.fillWidth: true
                                visible: modelData.currentSenderCallsign.length > 0
                                text: "CURRENT SENDER  "
                                      + modelData.currentSenderCallsign
                                      + (modelData.currentSenderWpm > 0
                                         ? "  •  "
                                           + modelData.currentSenderWpm.toFixed(0)
                                           + " WPM"
                                         : "")
                                color: "#64dff0"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                                ToolTip.visible: senderHelp.hovered
                                ToolTip.text: "Attributed only from an explicit decoded CALL1 DE CALL2 or CQ DE CALL handover"
                                HoverHandler { id: senderHelp }
                            }
                            ScrollView {
                                id: transcriptScroll
                                Layout.fillWidth: true
                                Layout.preferredHeight: 112
                                clip: true
                                property bool followTail: true
                                function maximumContentY() {
                                    if (!contentItem)
                                        return 0
                                    return Math.max(0, contentItem.contentHeight
                                                       - contentItem.height)
                                }
                                function isAtTail() {
                                    return !contentItem
                                           || contentItem.contentY
                                              >= maximumContentY() - 2
                                }
                                function pinToTail() {
                                    if (followTail && contentItem)
                                        contentItem.contentY = maximumContentY()
                                }
                                function followAppendedText() {
                                    if (decodedTextArea.selectionStart
                                            !== decodedTextArea.selectionEnd) {
                                        followTail = false
                                        return
                                    }
                                    if (!followTail)
                                        return
                                    // Pin immediately so the tail is already
                                    // correct in the frame the text grows in,
                                    // then again once layout settles, because
                                    // the content height for a wrapped line is
                                    // only final after that pass.
                                    pinToTail()
                                    Qt.callLater(function() {
                                        if (transcriptScroll.followTail
                                                && transcriptScroll.contentItem) {
                                            transcriptScroll.contentItem.contentY =
                                                transcriptScroll.maximumContentY()
                                        }
                                    })
                                }
                                ScrollBar.horizontal: ScrollBar {
                                    policy: ScrollBar.AlwaysOff
                                }
                                ScrollBar.vertical: ScrollBar {
                                    id: transcriptVerticalBar
                                    policy: ScrollBar.AlwaysOn
                                    onPressedChanged: {
                                        if (!pressed)
                                            transcriptScroll.followTail =
                                                transcriptScroll.isAtTail()
                                    }
                                }
                                Connections {
                                    target: transcriptScroll.contentItem
                                    function onMovementStarted() {
                                        // Wheel/touch scrolling is an explicit
                                        // request to inspect earlier output.
                                        transcriptScroll.followTail = false
                                    }
                                    function onMovementEnded() {
                                        transcriptScroll.followTail =
                                            transcriptScroll.isAtTail()
                                    }
                                    // Growing content would otherwise leave the
                                    // viewport short of the new bottom until
                                    // something else moved it.
                                    function onContentHeightChanged() {
                                        transcriptScroll.pinToTail()
                                    }
                                }
                                TextArea {
                                    id: decodedTextArea
                                    objectName: "decodedSessionText"
                                    readOnly: true
                                    selectByMouse: true
                                    width: transcriptScroll.availableWidth
                                    // Keep the transcript background equal to
                                    // the viewport when content is short, then
                                    // let it grow vertically for scrolling.
                                    height: Math.max(
                                                transcriptScroll.availableHeight,
                                                implicitHeight)
                                    text: ""
                                    textFormat: TextEdit.PlainText
                                    color: modelData.text.length > 0
                                           ? "#edf3f8"
                                           : (modelData.provisionalText.length > 0
                                              || modelData.elements.length > 0
                                              ? "#e3ad55" : "#8290a0")
                                    font.pixelSize: 18
                                    font.italic: modelData.text.length === 0
                                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                    padding: 8
                                    background: Rectangle {
                                        radius: 4
                                        color: "#0b121a"
                                        border.color: "#263241"
                                        border.width: 1
                                    }
                                    ToolTip.visible: transcriptHover.hovered
                                    ToolTip.delay: 500
                                    ToolTip.text: modelData.transmissions.length > 0
                                        ? "Completed transmissions are separated by |. Only conservative word gaps are repaired; decoded characters are unchanged."
                                        : "Live decoded text; select and scroll to pause automatic tail following"
                                    HoverHandler { id: transcriptHover }
                                    function applyDecodedText(nextText) {
                                        var oldSelectionStart = selectionStart
                                        var oldSelectionEnd = selectionEnd
                                        var hadSelection = oldSelectionStart
                                                           !== oldSelectionEnd
                                        // The transcript is append-only while a
                                        // station is being copied. Reassigning
                                        // the whole string rebuilds the text
                                        // document, which resets the viewport
                                        // and leaves the card showing a stale
                                        // offset until the next frame restores
                                        // it -- once per decoded character,
                                        // which reads as a constant shudder.
                                        // Insert only the new suffix so the
                                        // existing layout and scroll position
                                        // survive untouched.
                                        if (!hadSelection
                                                && nextText.length > length
                                                && nextText.indexOf(text)
                                                   === 0) {
                                            insert(length,
                                                   nextText.substring(length))
                                        } else {
                                            text = nextText
                                            if (hadSelection) {
                                                select(Math.min(oldSelectionStart, length),
                                                       Math.min(oldSelectionEnd, length))
                                            }
                                        }
                                        transcriptScroll.followAppendedText()
                                    }
                                    Component.onCompleted:
                                        applyDecodedText(
                                            sessionCard.displayedDecodedText)
                                    Connections {
                                        target: sessionCard
                                        function onDisplayedDecodedTextChanged() {
                                            decodedTextArea.applyDecodedText(
                                                sessionCard.displayedDecodedText)
                                        }
                                    }
                                }
                            }
                            Rectangle {
                                id: localModelTranscriptPanel
                                objectName: "localModelTranscriptPanel"
                                // Hidden when the optional local model is not
                                // in use. A panel reporting the state of a
                                // feature the operator has not set up is noise
                                // on every card, and it reported an error for a
                                // model that was never configured.
                                visible: sessionCard.localModelState !== "disabled"
                                         && sessionCard.localModelState !== "unconfigured"
                                Layout.fillWidth: true
                                Layout.preferredHeight: !visible ? 0
                                    : (sessionCard.localModelHasText ? 88 : 50)
                                radius: 4
                                color: "#101820"
                                border.color: sessionCard.localModelState
                                              === "error"
                                              ? "#c75b62" : "#263241"
                                border.width: 1
                                clip: true
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 7
                                    spacing: 3
                                    RowLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "LOCAL MODEL"
                                            color: "#91a0b1"
                                            font.pixelSize: 10
                                            font.weight: Font.Bold
                                        }
                                        Item { Layout.fillWidth: true }
                                        Label {
                                            objectName: "localModelStateLabel"
                                            text: sessionCard.localModelState.toUpperCase()
                                            color: sessionCard.localModelState
                                                   === "error"
                                                   ? "#ef7d85" : "#8290a0"
                                            font.pixelSize: 9
                                        }
                                    }
                                    Label {
                                        objectName: "localModelStatusLabel"
                                        visible: !sessionCard.localModelHasText
                                        Layout.fillWidth: true
                                        text: sessionCard.localModelStatus
                                        color: "#8290a0"
                                        font.pixelSize: 11
                                        elide: Text.ElideRight
                                    }
                                    ScrollView {
                                        id: localModelTranscriptScroll
                                        visible: sessionCard.localModelHasText
                                        Layout.fillWidth: true
                                        Layout.fillHeight: true
                                        clip: true
                                        property bool followTail: true
                                        function maximumContentY() {
                                            if (!contentItem)
                                                return 0
                                            return Math.max(
                                                0, contentItem.contentHeight
                                                   - contentItem.height)
                                        }
                                        function pinToTail() {
                                            if (followTail && contentItem)
                                                contentItem.contentY = maximumContentY()
                                        }
                                        function followAppendedText() {
                                            if (!followTail)
                                                return
                                            pinToTail()
                                            Qt.callLater(function() {
                                                if (localModelTranscriptScroll.followTail
                                                        && localModelTranscriptScroll.contentItem) {
                                                    localModelTranscriptScroll.contentItem.contentY =
                                                        localModelTranscriptScroll.maximumContentY()
                                                }
                                            })
                                        }
                                        ScrollBar.horizontal: ScrollBar {
                                            policy: ScrollBar.AlwaysOff
                                        }
                                        ScrollBar.vertical: ScrollBar {
                                            policy: ScrollBar.AlwaysOn
                                        }
                                        Connections {
                                            target: localModelTranscriptScroll.contentItem
                                            function onMovementStarted() {
                                                localModelTranscriptScroll.followTail = false
                                            }
                                            function onMovementEnded() {
                                                localModelTranscriptScroll.followTail =
                                                    localModelTranscriptScroll.contentItem.contentY
                                                    >= localModelTranscriptScroll.maximumContentY() - 2
                                            }
                                            function onContentHeightChanged() {
                                                localModelTranscriptScroll.pinToTail()
                                            }
                                        }
                                        TextArea {
                                            id: localModelTranscriptText
                                            objectName: "localModelTranscriptText"
                                            readOnly: true
                                            selectByMouse: true
                                            width: localModelTranscriptScroll.availableWidth
                                            height: Math.max(
                                                        localModelTranscriptScroll.availableHeight,
                                                        implicitHeight)
                                            text: ""
                                            textFormat: TextEdit.PlainText
                                            color: "#c8e6df"
                                            font.pixelSize: 14
                                            wrapMode: TextEdit.WrapAnywhere
                                            padding: 0
                                            background: null
                                            // Append-only applies within one
                                            // lane incarnation. A withdrawn
                                            // or non-prefix value marks a
                                            // lifecycle reset and must clear
                                            // stale text from a retained card.
                                            function applyStableText(nextText) {
                                                if (sessionCard.localModelState
                                                        !== "ready"
                                                        || nextText.length === 0) {
                                                    text = ""
                                                    localModelTranscriptScroll.followTail = true
                                                } else if (nextText.indexOf(text)
                                                           === 0) {
                                                    // Append only the suffix,
                                                    // for the same reason the
                                                    // decoded transcript does.
                                                    if (nextText.length > length)
                                                        insert(length,
                                                               nextText.substring(length))
                                                } else {
                                                    text = nextText
                                                    localModelTranscriptScroll.followTail = true
                                                }
                                                localModelTranscriptScroll.followAppendedText()
                                            }
                                            Component.onCompleted:
                                                applyStableText(
                                                    sessionCard.localModelStableText)
                                            Connections {
                                                target: sessionCard
                                                function onLocalModelStableTextChanged() {
                                                    localModelTranscriptText.applyStableText(
                                                        sessionCard.localModelStableText)
                                                }
                                                function onLocalModelStateChanged() {
                                                    localModelTranscriptText.applyStableText(
                                                        sessionCard.localModelStableText)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            Label {
                                objectName: "decoderMetricsLabel"
                                Layout.fillWidth: true
                                // Readable while operating. This was 10px
                                // low-contrast grey on one elided row, so the
                                // values were cut off and hard to read at all.
                                // Confidence now reports the character-averaged
                                // figure: the instantaneous one falls to zero
                                // between characters, so the line read 0%
                                // while text was arriving. The instantaneous
                                // key state is dropped entirely; it is already
                                // shown by the keyed marker, and as a number it
                                // only ever flickers.
                                text: (modelData.wpm > 0
                                       ? modelData.wpm.toFixed(0) + " WPM"
                                       : "WPM —")
                                      + "   •   " + modelData.snrDb.toFixed(0)
                                      + " dB   •   "
                                      + modelData.filterWidthHz.toFixed(0)
                                      + " Hz   •   "
                                      + (modelData.meanCharacterConfidence * 100).toFixed(0)
                                      + "% confidence"
                                color: "#c8d4e0"
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }

    Drawer {
        id: txDrawer
        edge: Qt.LeftEdge
        width: Math.min(window.width * 0.46, 620)
        height: window.height
        modal: true
        background: Rectangle { color: "#111720" }
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: Math.max(0, parent.width - 40)
                x: 20
                spacing: 12
                Label {
                    text: "Guarded TX / QSO"
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#91a0b1"
                    text: "Decoder output may suggest an action, but it cannot key the transmitter. Arm TX, exactly confirm the station, then confirm every normalized message preview."
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        objectName: "txArmButton"
                        text: transmitController.armed ? "Disarm TX" : "Arm TX"
                        onClicked: transmitController.armed
                                   ? transmitController.disarm()
                                   : transmitController.arm()
                        ToolTip.visible: hovered
                        ToolTip.text: transmitController.armed
                            ? "Disarm transmission and clear pending actions"
                            : "Enter the guarded TX workflow; this does not key hardware"
                    }
                    CheckBox {
                        objectName: "autoQsoModeCheck"
                        text: "Auto-QSO suggestions"
                        checked: transmitController.autoQsoEnabled
                        enabled: transmitController.armed
                        onToggled: transmitController.autoQsoEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: enabled
                            ? "Suggest context-matched replies; every message still requires exact confirmation"
                            : "Arm TX before enabling reply suggestions"
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: "EMERGENCY RELEASE"
                        highlighted: true
                        onClicked: transmitController.emergencyRelease()
                        ToolTip.visible: hovered
                        ToolTip.text: "Immediately release KEY/PTT and latch a fault"
                    }
                    Button {
                        objectName: "txTuneButton"
                        text: transmitController.tuning ? "STOP TUNE" : "TUNE"
                        enabled: transmitController.armed
                        highlighted: transmitController.tuning
                        onClicked: transmitController.toggleTune()
                        ToolTip.visible: hovered
                        ToolTip.text: "Operator-only KEY/tone toggle; hard 15-second watchdog"
                    }
                }
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: transmitController.state === "fault"
                           ? "#ff7b84" : "#f3bd55"
                    text: transmitController.status
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: transmitController.state === "fault"
                    Button {
                        text: "Reset fault (stays disarmed)"
                        onClicked: transmitController.resetFault()
                        ToolTip.visible: hovered
                        ToolTip.text: "Clear the latched fault without arming transmission"
                    }
                }
                Label {
                    text: transmitController.targetCallsign.length > 0
                          ? "Selected station: " + transmitController.targetCallsign
                          : "Select TX on an exactly decoded receiver card"
                    color: "#62ffa2"
                    font.pixelSize: 17
                    font.weight: Font.Bold
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: transmitController.targetCallsign.length > 0
                             && !transmitController.qsoConfirmed
                    TextField {
                        id: txCallConfirmation
                        objectName: "txCallConfirmationField"
                        Layout.fillWidth: true
                        placeholderText: "Retype callsign exactly"
                        selectByMouse: true
                    }
                    Button {
                        text: "Confirm station"
                        onClicked: transmitController.confirmTarget(
                                       txCallConfirmation.text)
                        ToolTip.visible: hovered
                        ToolTip.text: "Accept only an exact retype of the selected decoded callsign"
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: transmitController.qsoConfirmed
                    Button {
                        text: "Send my call"
                        onClicked: transmitController.prepareOwnCall()
                        ToolTip.visible: hovered
                        ToolTip.text: "Prepare your configured callsign for exact preview confirmation"
                    }
                    Button {
                        text: "Send report " + transmitController.report
                        onClicked: transmitController.prepareReport()
                        ToolTip.visible: hovered
                        ToolTip.text: "Prepare the displayed signal report for exact preview confirmation"
                    }
                    Button {
                        text: "End QSO"
                        onClicked: transmitController.endQso()
                        ToolTip.visible: hovered
                        ToolTip.text: "Clear the selected station and pending exchange"
                    }
                }
                Button {
                    objectName: "anchorPileupRunnerButton"
                    Layout.fillWidth: true
                    visible: transmitController.targetCallsign.length > 0
                    enabled: transmitController.targetRfHz > 0
                             && appSettings.radioFrequencyWritable
                    text: "Anchor runner at "
                          + appSettings.cwGuideCenterHz.toFixed(0) + " Hz"
                    onClicked: appSettings.setControlledRxFrequency(
                                   (transmitController.targetRfHz / 1000)
                                     .toFixed(3), 1000)
                    ToolTip.visible: hovered
                    ToolTip.text: enabled
                        ? "Retune RX so this runner falls on the CW guide; TX and split remain unchanged"
                        : "Requires a checked RF marker and a writable linked radio"
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: transmitController.proposedMessage.length > 0
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: transmitController.proposedReason + ": "
                              + transmitController.proposedMessage
                        color: "#ffd54f"
                    }
                    Button {
                        text: "Prepare"
                        onClicked: transmitController.acceptProposal()
                        ToolTip.visible: hovered
                        ToolTip.text: "Move this suggestion into the exact confirmation preview"
                    }
                }
                Label { text: "Free text"; font.weight: Font.Bold }
                TextArea {
                    id: txFreeText
                    objectName: "txFreeTextArea"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    enabled: transmitController.qsoConfirmed
                    placeholderText: "Type operator-authored CW text"
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: "Prepare free text"
                        enabled: transmitController.qsoConfirmed
                        onClicked: transmitController.prepareFreeText(txFreeText.text)
                        ToolTip.visible: hovered
                        ToolTip.text: enabled
                            ? "Normalize this operator-authored text and open exact preview confirmation"
                            : "Confirm the selected station first"
                    }
                    Label { text: "WPM" }
                    Slider {
                        from: 5
                        to: 80
                        stepSize: 1
                        value: transmitController.wordsPerMinute
                        onMoved: transmitController.wordsPerMinute = Math.round(value)
                        Layout.fillWidth: true
                        ToolTip.visible: hovered
                        ToolTip.text: "Transmit speed " + Math.round(value) + " WPM"
                    }
                    Label { text: transmitController.wordsPerMinute }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: previewColumn.implicitHeight + 24
                    visible: transmitController.preparedMessage.length > 0
                    radius: 6
                    color: "#07110e"
                    border.color: transmitController.messageConfirmed
                                  ? "#4dff88" : "#f3bd55"
                    ColumnLayout {
                        id: previewColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        Label { text: "EXACT TX PREVIEW"; color: "#91a0b1"; font.weight: Font.Bold }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WrapAnywhere
                            text: transmitController.preparedMessage
                            color: "#ffffff"
                            font.family: "monospace"
                            font.pixelSize: 18
                        }
                        Label {
                            text: transmitController.previewDurationSeconds.toFixed(2)
                                  + " s at " + transmitController.wordsPerMinute + " WPM"
                            color: "#91a0b1"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                id: txPreviewConfirmation
                                objectName: "txPreviewConfirmationField"
                                Layout.fillWidth: true
                                placeholderText: "Retype preview exactly"
                                enabled: !transmitController.messageConfirmed
                            }
                            Button {
                                text: "Confirm preview"
                                enabled: !transmitController.messageConfirmed
                                onClicked: transmitController.confirmPreview(
                                               txPreviewConfirmation.text)
                                ToolTip.visible: hovered
                                ToolTip.text: "Accept only an exact retype of the normalized message"
                            }
                        }
                    }
                }
                Button {
                    objectName: "transmitPreparedButton"
                    Layout.fillWidth: true
                    text: transmitController.hardwareAvailable
                          ? "TRANSMIT PREPARED MESSAGE"
                          : "KEY/PTT ADAPTER NOT YET AVAILABLE"
                    enabled: transmitController.messageConfirmed
                             && transmitController.hardwareAvailable
                    onClicked: transmitController.transmitPrepared()
                    ToolTip.visible: hovered
                    ToolTip.text: transmitController.hardwareAvailable
                        ? "Transmit the exactly confirmed message through the guarded adapter"
                        : "No tested KEY/PTT hardware adapter is available in this build"
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

    // Startup notice for pending updates. It appears once per launch, only
    // after any first-run setup is out of the way, and never steals focus from
    // a decode in progress: the operator is told and can act later.
    Dialog {
        id: updateNotice
        objectName: "updateNoticeDialog"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(460, parent ? parent.width - 48 : 460)
        title: "Updates available"
        standardButtons: Dialog.Close
        property bool shownThisLaunch: false
        property bool appDownloadStarted: false
        property bool appPending: updateChecker.updateAvailable
        property bool listPending: callsignDatabaseUpdater.updateAvailable
        function considerShowing() {
            if (shownThisLaunch) return
            if (appSettings.profileSelectionRequired) return
            if (!appSettings.setupComplete) return
            if (!appPending && !listPending) return
            shownThisLaunch = true
            open()
        }
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#c8d4e0"
                text: "The following updates are ready. Installing them is optional and nothing is downloaded until you choose to."
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: updateNotice.appPending
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#edf3f8"
                    text: "Application " + updateChecker.latestVersion
                          + " (installed " + updateChecker.currentVersion + ")"
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        objectName: "updateNoticeDownloadAppButton"
                        visible: updateChecker.downloadActionVisible
                        text: updateChecker.downloading
                              ? "Downloading… "
                                + Math.round(updateChecker.downloadProgress * 100)
                                + "%"
                              : "Download update"
                        enabled: !updateChecker.downloading
                        onClicked: {
                            updateNotice.appDownloadStarted = true
                            updateChecker.downloadUpdate()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Download this platform package and verify its SHA-256 checksum"
                    }
                    Button {
                        objectName: "updateNoticeOpenAppButton"
                        visible: updateChecker.verifiedDownloadActionsVisible
                        text: "Open Installer"
                        onClicked: updateChecker.openDownloadedFile()
                        ToolTip.visible: hovered
                        ToolTip.text: "Open the verified package with the operating-system installer"
                    }
                    Button {
                        objectName: "updateNoticeRevealAppButton"
                        visible: updateChecker.verifiedDownloadActionsVisible
                        text: Qt.platform.os === "osx" ? "Show in Finder"
                              : Qt.platform.os === "windows"
                                ? "Show in File Explorer"
                                : "Show in Folder"
                        flat: true
                        onClicked: updateChecker.revealDownloadFolder()
                        ToolTip.visible: hovered
                        ToolTip.text: "Show the verified package in the file manager"
                    }
                    Item { Layout.fillWidth: true }
                }
            }
            Label {
                objectName: "updateNoticeAppStatusLabel"
                Layout.fillWidth: true
                visible: updateNotice.appPending
                         && (updateNotice.appDownloadStarted
                             || updateChecker.downloading
                             || updateChecker.downloadVerified)
                wrapMode: Text.WordWrap
                color: updateChecker.downloadVerified ? "#4dff88" : "#91a0b1"
                text: updateChecker.statusMessage
            }
            RowLayout {
                Layout.fillWidth: true
                visible: updateNotice.listPending
                spacing: 8
                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: "#edf3f8"
                    text: "Offline callsign list"
                }
                Button {
                    objectName: "updateNoticeUpdateListButton"
                    text: callsignDatabaseUpdater.downloading ? "Updating" : "Update"
                    enabled: !callsignDatabaseUpdater.downloading
                    onClicked: callsignDatabaseUpdater.updateDatabase()
                    ToolTip.visible: hovered
                    ToolTip.text: "Download and atomically verify the newer offline callsign list"
                }
            }
        }
    }

    Connections {
        target: updateChecker
        function onStateChanged() { updateNotice.considerShowing() }
    }

    Connections {
        target: callsignDatabaseUpdater
        function onStateChanged() { updateNotice.considerShowing() }
    }

    FileDialog {
        id: wavDialog
        title: "Open receiver WAV recording"
        fileMode: FileDialog.OpenFile
        nameFilters: ["WAV audio (*.wav *.wave)", "All files (*)"]
        onAccepted: replayController.openFile(selectedFile)
    }

    Component.onCompleted: {
        showMaximized()
        if (appSettings.profileSelectionRequired)
            profileChooser.open()
        else if (!appSettings.setupComplete)
            setupWizard.open()
        else
            updateNotice.considerShowing()
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
