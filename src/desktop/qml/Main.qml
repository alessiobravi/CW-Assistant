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

    // A VFO-style readout deserves the same decimal/centesimal precision a
    // real rig's display has (e.g. 7016.45 kHz on 40 m), which the coarser
    // formatFrequency() above deliberately does not provide for spectrum
    // axis labels.
    function formatVfoFrequency(hz) {
        if (Math.abs(hz) >= 30000000)
            return (hz / 1000000).toFixed(5) + " MHz"
        return (hz / 1000).toFixed(2) + " kHz"
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
                id: receiverToolbar
                Layout.fillWidth: true
                z: 20
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
                    z: 21
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
                    ToolTip.delay: 650
                    ToolTip.text: hoveredStreamId !== 0
                                  ? "Left-click to open this decoded stream"
                                  : "Right-click an unmarked signal to open a manual decoding slice"
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
                            text: !modelData.verifiedCw
                                  ? modelData.frequencyLabel + " • manual"
                                  : modelData.callsign.length > 0
                                  ? modelData.callsign
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
                                color: "#e6091018"
                                border.color: modelData.color
                                border.width: channelMarker.pointerHovered ? 1 : 0
                                radius: 3
                            }
                            Behavior on font.pixelSize {
                                NumberAnimation { duration: 90 }
                            }
                        }
                        ToolTip.visible: channelMarker.pointerHovered
                        ToolTip.delay: 450
                        ToolTip.text: (modelData.callsign.length > 0
                                       ? modelData.callsign + "\n" : "")
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
                        Layout.alignment: Qt.AlignHCenter
                        text: replayController.sourceMode === 0 ? "Start live RX" : "Choose WAV recording"
                        onClicked: replayController.sourceMode === 0 ? replayController.startLiveAudio() : wavDialog.open()
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
                        Button { text: "Save profile"; onClicked: appSettings.apply() }
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
            Layout.preferredWidth: 390
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
                        ToolTip.text: "Records raw live audio and per-track decoder internals to disk for troubleshooting a signal that will not decode. Capped at 5 minutes. Review the saved files before sharing them — the audio is whatever the selected input picked up."
                    }
                }
                ColumnLayout {
                    id: vfoDisplay
                    objectName: "vfoDisplay"
                    // The VFO readout only means anything with a live,
                    // connected radio driving the audio (CAT/OmniRig); it is
                    // hidden entirely for receive-only SWL setups and WAV
                    // replay, where there is no radio state to show.
                    visible: replayController.radioFrequencyAvailable
                             && replayController.sourceMode === 0
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        spacing: 10
                        Label {
                            objectName: "vfoRxLabel"
                            text: "RX  " + window.formatVfoFrequency(replayController.radioRxFrequencyHz)
                            color: "#4dff88"
                            font.pixelSize: 28
                            font.weight: Font.Bold
                            font.letterSpacing: 1
                        }
                        Rectangle {
                            objectName: "vfoSplitBadge"
                            visible: replayController.radioSplitActive
                            radius: 4
                            color: "#3a2f10"
                            border.color: "#ffe14d"
                            border.width: 1
                            implicitWidth: splitBadgeLabel.implicitWidth + 12
                            implicitHeight: splitBadgeLabel.implicitHeight + 6
                            Label {
                                id: splitBadgeLabel
                                anchors.centerIn: parent
                                text: "SPLIT"
                                color: "#ffe14d"
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }
                        }
                        Rectangle {
                            id: onAirIndicator
                            objectName: "onAirIndicator"
                            // Placeholder only: no backend currently reports
                            // live PTT/transmit state (neither the CAT4OM
                            // protocol nor the OmniRig properties this app
                            // polls expose it, and local keying/PTT hardware
                            // control is not implemented yet — see KEY-001,
                            // SAFE-001 in BACKLOG.md). "active" is wired up
                            // for real once that telemetry exists; until
                            // then this always renders unlit.
                            property bool active: false
                            radius: 6
                            implicitWidth: onAirRow.implicitWidth + 16
                            implicitHeight: onAirRow.implicitHeight + 8
                            color: active ? "#4d0d0d" : "#1c2229"
                            border.color: active ? "#ff3b30" : "#3a4552"
                            border.width: 1
                            RowLayout {
                                id: onAirRow
                                anchors.centerIn: parent
                                spacing: 6
                                Rectangle {
                                    width: 10
                                    height: 10
                                    radius: 5
                                    color: onAirIndicator.active ? "#ff3b30" : "#4a1414"
                                    border.color: onAirIndicator.active ? "#ff8a80" : "#5c2020"
                                    border.width: 1
                                }
                                Label {
                                    text: "ON AIR"
                                    color: onAirIndicator.active ? "#ff8a80" : "#5c6a78"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                    font.letterSpacing: 1
                                }
                            }
                            ToolTip.visible: onAirMouse.containsMouse
                            ToolTip.delay: 300
                            ToolTip.text: "Placeholder — not wired to live transmit state yet; no radio backend currently reports it."
                            MouseArea {
                                id: onAirMouse
                                anchors.fill: parent
                                hoverEnabled: true
                            }
                        }
                    }
                    Label {
                        objectName: "vfoTxLabel"
                        visible: replayController.radioSplitActive
                        text: "TX  " + window.formatVfoFrequency(replayController.radioTxFrequencyHz)
                        color: "#ffe14d"
                        font.pixelSize: 28
                        font.weight: Font.Bold
                        font.letterSpacing: 1
                    }
                }
                Label {
                    visible: replayController.debugCaptureActive || replayController.debugCapturePath.length > 0
                    text: replayController.debugCaptureActive
                          ? "Capturing… " + replayController.debugCaptureElapsedSeconds.toFixed(0) + "s / 300s max — " + replayController.debugCapturePath
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
                        width: decoderChannelList.width
                        height: 210
                        radius: 7
                        color: "#151d27"
                        border.width: modelData.keyDown ? 2 : 1
                        border.color: modelData.color
                        z: 1
                        property string rawDecodedText: modelData.text.length > 0
                            ? modelData.text
                            : (modelData.provisionalText.length > 0
                               ? modelData.provisionalText
                               : (modelData.elements.length > 0
                                  ? modelData.elements
                                  : (!modelData.verifiedCw
                                     ? "Analyzing the selected frequency…"
                                     : "Listening…")))
                        // Acoustic alternatives remain available to callsign
                        // evidence and diagnostics, but do not sit below the
                        // continuously growing literal transcript. A bounded
                        // consensus can legitimately abstain while raw text
                        // continues, which otherwise makes the viewport look
                        // frozen at the older consensus tail.
                        property string displayedDecodedText: rawDecodedText
                        property string callsignEvidenceText:
                            rawDecodedText + " " + modelData.refinedText
                        property int ownCallMatches: window.exactCallCount(
                            callsignEvidenceText, appSettings.ownCallsign)
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
                                    text: modelData.callsign.length > 0
                                          ? modelData.callsign + "  •  "
                                            + modelData.frequencyLabel
                                          : modelData.frequencyLabel
                                    color: modelData.color
                                    font.weight: Font.Bold
                                    font.pixelSize: 16
                                }
                                Label {
                                    visible: sessionCard.ownCallMatches > 0
                                    text: "YOUR CALL HEARD"
                                    color: "#ffd54f"
                                    font.pixelSize: 11
                                    font.weight: Font.Bold
                                }
                                Item { Layout.fillWidth: true }
                                Label {
                                    text: !modelData.verifiedCw
                                          ? "MANUAL"
                                          : (modelData.active ? "ACTIVE" : "HOLD")
                                    color: modelData.active ? modelData.color : "#718091"
                                    font.pixelSize: 10
                                }
                                ToolButton {
                                    objectName: "moveDecoderSessionUpButton"
                                    text: "↑"
                                    enabled: sessionCard.index > 0
                                    Accessible.name: "Move decoded session up"
                                    onPressed: replayController.moveDecoderSession(
                                                   modelData.id,
                                                   sessionCard.index - 1)
                                }
                                ToolButton {
                                    objectName: "moveDecoderSessionDownButton"
                                    text: "↓"
                                    enabled: sessionCard.index + 1
                                             < decoderChannelList.count
                                    Accessible.name: "Move decoded session down"
                                    onPressed: replayController.moveDecoderSession(
                                                   modelData.id,
                                                   sessionCard.index + 1)
                                }
                                ToolButton {
                                    objectName: "closeDecoderSessionButton"
                                    text: "×"
                                    z: 40
                                    Accessible.name: "Close decoded session"
                                    onPressed: replayController.closeDecoderSession(
                                                   modelData.id)
                                }
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
                                function followAppendedText() {
                                    if (decodedTextArea.selectionStart
                                            !== decodedTextArea.selectionEnd) {
                                        followTail = false
                                        return
                                    }
                                    if (!followTail)
                                        return
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
                                }
                                TextArea {
                                    id: decodedTextArea
                                    objectName: "decodedSessionText"
                                    readOnly: true
                                    selectByMouse: true
                                    width: transcriptScroll.availableWidth
                                    text: ""
                                    textFormat: TextEdit.PlainText
                                    color: modelData.text.length > 0
                                           ? "#edf3f8"
                                           : (modelData.provisionalText.length > 0
                                              || modelData.elements.length > 0
                                              ? "#e3ad55" : "#8290a0")
                                    font.pixelSize: 18
                                    font.italic: modelData.text.length === 0
                                    wrapMode: TextEdit.WrapAnywhere
                                    padding: 8
                                    background: Rectangle {
                                        radius: 4
                                        color: "#0b121a"
                                        border.color: "#263241"
                                        border.width: 1
                                    }
                                    function applyDecodedText(nextText) {
                                        var oldSelectionStart = selectionStart
                                        var oldSelectionEnd = selectionEnd
                                        var hadSelection = oldSelectionStart
                                                           !== oldSelectionEnd
                                        text = nextText
                                        if (hadSelection) {
                                            select(Math.min(oldSelectionStart,
                                                            length),
                                                   Math.min(oldSelectionEnd,
                                                            length))
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
