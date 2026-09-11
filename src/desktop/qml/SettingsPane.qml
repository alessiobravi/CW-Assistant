import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Pane {
    id: root
    signal done()
    signal setupRequested()
    property bool sdrDiscoveryRequested: false

    function formatFrequencyKhz(frequencyHz) {
        return Number((Number(frequencyHz) / 1000).toFixed(3)).toString()
    }

    function parseFrequencyKhz(value) {
        var khz = Number(String(value).replace(",", "."))
        if (!Number.isFinite(khz) || khz <= 0 || khz > 99000000)
            return 0
        return Math.round(khz * 1000)
    }

    function requestInitialSdrDiscovery() {
        if (sdrDiscoveryRequested)
            return
        sdrDiscoveryRequested = true
        // Let the SDR page render before a vendor module probes USB. Discovery
        // remains receive-only and does not open or start any returned device.
        Qt.callLater(function() { appSettings.refreshSdrDevices() })
    }

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
            ToolButton {
                text: "Close"; onClicked: root.done()
                ToolTip.visible: hovered
                ToolTip.text: "Close Settings; unapplied edits remain unsaved"
            }
        }

        TabBar {
            id: tabs
            Layout.fillWidth: true
            onCurrentIndexChanged: {
                if (currentIndex === 1)
                    root.requestInitialSdrDiscovery()
            }
            TabButton { text: "Audio" }
            TabButton { text: "SDR" }
            TabButton { text: "Decoder" }
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
                    Label { text: "Monitor output" }
                    ComboBox {
                        objectName: "audioMonitorOutputCombo"
                        Layout.fillWidth: true
                        model: appSettings.audioOutputNames
                        currentIndex: appSettings.audioOutputIndex
                        onActivated: appSettings.selectAudioOutput(currentIndex)
                    }
                    Label { text: "Selected output" }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: appSettings.audioOutputDisplayName; color: "#43c6ac" }
                    Label { text: "Radio audio association" }
                    CheckBox {
                        text: "This input carries RX audio from the configured radio"
                        checked: appSettings.audioInputRadioLinked
                        enabled: appSettings.radioEnabled
                        onToggled: appSettings.audioInputRadioLinked = checked
                    }
                    Label { text: "" }
                    Button {
                        text: "Refresh audio devices"
                        onClicked: {
                            appSettings.refreshAudioInputs()
                            appSettings.refreshAudioOutputs()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Rescan operating-system audio input and monitor-output devices"
                    }
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
                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Select direct, receive-only SDR input for a much wider RF passband than sound-card audio. SDR reception never exposes transmit, PTT, or KEY control."
                    }
                    Label { text: "Receiver source" }
                    ComboBox {
                        objectName: "receiverInputTypeCombo"
                        Layout.fillWidth: true
                        model: appSettings.receiverInputTypeNames
                        currentIndex: appSettings.receiverInputTypeIndex
                        enabled: appSettings.sdrBackendAvailable
                                 && appSettings.sdrDeviceNames.length > 0
                        onActivated: appSettings.receiverInputTypeIndex = currentIndex
                        ToolTip.visible: hovered
                        ToolTip.text: appSettings.sdrBackendAvailable
                            ? "Choose conventional sound-card audio or a directly connected wide-passband SDR"
                            : "SDR is unavailable in this build; sound-card audio remains fully operational"
                    }
                    Label { text: "SoapySDR backend" }
                    Label {
                        objectName: "sdrBackendStateLabel"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: appSettings.sdrBackendAvailable ? "#43c6ac" : "#f3bd55"
                        text: appSettings.sdrBackendAvailable
                            ? "Available" + (appSettings.sdrBackendVersion.length > 0
                                ? "  •  " + appSettings.sdrBackendVersion : "")
                            : "Unavailable — sound-card audio remains selected"
                    }
                    Label { text: "Installed modules" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: appSettings.sdrModuleNames.length > 0
                            ? appSettings.sdrModuleNames.join(", ")
                            : "No SoapySDR receiver modules detected"
                    }
                    Label { text: "Physical SDR" }
                    ComboBox {
                        objectName: "sdrDeviceCombo"
                        Layout.fillWidth: true
                        model: appSettings.sdrDeviceNames
                        currentIndex: appSettings.sdrDeviceIndex
                        enabled: appSettings.sdrBackendAvailable
                                 && appSettings.sdrDeviceNames.length > 0
                        onActivated: appSettings.selectSdrDevice(currentIndex)
                        ToolTip.visible: hovered
                        ToolTip.text: enabled
                            ? "Select one physical receive-only SDR; alternative operating modes of the same serial number are grouped together"
                            : "Install the matching SoapySDR hardware module, connect the receiver, then refresh"
                    }
                    Label { text: "Operating mode" }
                    ComboBox {
                        objectName: "sdrOperatingModeCombo"
                        Layout.fillWidth: true
                        model: appSettings.sdrOperatingModeNames
                        currentIndex: appSettings.sdrOperatingModeIndex
                        enabled: appSettings.sdrOperatingModeNames.length > 1
                        onActivated:
                            appSettings.selectSdrOperatingMode(currentIndex)
                        ToolTip.visible: hovered
                        ToolTip.text: appSettings.sdrOperatingModeNames.length > 1
                            ? "Choose a hardware operating configuration. Single tuner is recommended for CW Buddy's current one-channel receive path."
                            : "This receiver exposes one operating configuration"
                    }
                    Label { text: "Mode details" }
                    Label {
                        objectName: "sdrOperatingModeHelp"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        property string selectedMode: appSettings.sdrOperatingModeIndex >= 0
                            ? appSettings.sdrOperatingModeNames[appSettings.sdrOperatingModeIndex]
                            : ""
                        text: selectedMode.indexOf("ST ") === 0
                            ? "Single tuner is the recommended one-channel CW Buddy mode and can select either tuner input exposed by the driver."
                            : selectedMode.indexOf("DT ") === 0
                              ? "Dual tuner provides two synchronized RX channels; CW Buddy currently consumes channel 0 only."
                              : selectedMode.indexOf("MA8 ") === 0
                                ? "Master mode using the alternative 8 MHz master sample clock for coordinated master/slave operation."
                                : selectedMode.indexOf("MA ") === 0
                                  ? "Master mode using the 6 MHz master sample clock for coordinated master/slave operation."
                                  : selectedMode.indexOf("SL ") === 0
                                    ? "Slave is an advanced mode controlled by a coordinated RSPduo master process."
                                    : "The receiver exposes a single default operating configuration."
                    }
                    Label { text: "Discovery" }
                    RowLayout {
                        Layout.fillWidth: true
                        Button {
                            objectName: "refreshSdrDevicesButton"
                            text: "Refresh devices"
                            onClicked: appSettings.refreshSdrDevices()
                            ToolTip.visible: hovered
                            ToolTip.text: "Rescan SoapySDR modules and attached receivers without starting reception"
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: appSettings.sdrBackendAvailable ? "#91a0b1" : "#f3bd55"
                            text: appSettings.sdrDiagnostic
                        }
                    }
                    Label { text: "SDR center frequency (kHz)" }
                    TextField {
                        objectName: "sdrCenterFrequencyField"
                        Layout.fillWidth: true
                        enabled: appSettings.sdrBackendAvailable
                                 && !appSettings.sdrFollowRadioVfo
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        text: root.formatFrequencyKhz(
                                  appSettings.sdrCenterFrequencyHz)
                        placeholderText: "7021.43"
                        validator: RegularExpressionValidator {
                            regularExpression: /[0-9]{1,8}([.,][0-9]{0,3})?/
                        }
                        onEditingFinished: {
                            var frequencyHz = root.parseFrequencyKhz(text)
                            if (frequencyHz > 0)
                                appSettings.sdrCenterFrequencyHz = frequencyHz
                            text = root.formatFrequencyKhz(
                                appSettings.sdrCenterFrequencyHz)
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Enter VFO-style kHz (for example 7021.43 means 7.02143 MHz). Radio-follow mode updates this from authoritative CAT readback."
                    }
                    Label { text: "IQ sample rate" }
                    ComboBox {
                        objectName: "sdrSampleRateCombo"
                        Layout.fillWidth: true
                        enabled: appSettings.sdrBackendAvailable
                        editable: true
                        model: appSettings.sdrSampleRateOptions.length > 0
                            ? appSettings.sdrSampleRateOptions
                            : [62500, 96000, 125000, 192000, 250000,
                               384000, 500000, 768000, 1000000,
                               2000000, 2400000, 8000000, 10000000]
                        currentIndex: model.indexOf(appSettings.sdrSampleRateHz)
                        displayText: appSettings.sdrSampleRateHz + " Hz"
                        onActivated: appSettings.sdrSampleRateHz = currentValue
                        onAccepted: appSettings.sdrSampleRateHz = Number(editText.replace(/[^0-9]/g, ""))
                        ToolTip.visible: hovered
                        ToolTip.text: "Effective IQ output rate. Drivers such as SDRplay apply their supported hardware decimation automatically for lower rates."
                    }
                    Label { text: "Hardware RF bandwidth" }
                    ComboBox {
                        objectName: "sdrBandwidthCombo"
                        Layout.fillWidth: true
                        enabled: appSettings.sdrBackendAvailable
                        editable: true
                        model: appSettings.sdrBandwidthOptions.length > 1
                            ? appSettings.sdrBandwidthOptions
                            : [0, 200000, 300000, 600000, 1536000,
                               5000000, 6000000, 7000000, 8000000]
                        currentIndex: model.indexOf(appSettings.sdrBandwidthHz)
                        displayText: appSettings.sdrBandwidthHz === 0
                            ? "Automatic" : appSettings.sdrBandwidthHz + " Hz"
                        onActivated: appSettings.sdrBandwidthHz = currentValue
                        onAccepted: appSettings.sdrBandwidthHz =
                            Number(editText.replace(/[^0-9]/g, ""))
                        ToolTip.visible: hovered
                        ToolTip.text: "Requested analogue/baseband RF filter width; the provider selects the nearest supported value"
                    }
                    Label { text: "Driver decimation" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#43c6ac"
                        text: "Automatic through the effective IQ sample rate ("
                              + appSettings.sdrSampleRateHz + " S/s requested)"
                    }
                    Label { text: "Antenna / tuner input" }
                    ComboBox {
                        objectName: "sdrAntennaCombo"
                        Layout.fillWidth: true
                        model: appSettings.sdrAntennaNames
                        currentIndex: appSettings.sdrAntennaIndex
                        enabled: appSettings.sdrAntennaNames.length > 0
                        onActivated: appSettings.selectSdrAntenna(currentIndex)
                        ToolTip.visible: hovered
                        ToolTip.text: enabled
                            ? "Receiver input exposed by the selected SDR operating mode"
                            : "This SDR driver does not expose an antenna selector"
                    }
                    Label { text: "Decoder window center (kHz)" }
                    TextField {
                        objectName: "sdrDecoderCenterFrequencyField"
                        Layout.fillWidth: true
                        enabled: appSettings.sdrBackendAvailable
                                 && !appSettings.sdrFollowRadioVfo
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        text: root.formatFrequencyKhz(
                                  appSettings.sdrDecoderCenterFrequencyHz)
                        validator: RegularExpressionValidator {
                            regularExpression: /[0-9]{1,8}([.,][0-9]{0,3})?/
                        }
                        onEditingFinished: {
                            var frequencyHz = root.parseFrequencyKhz(text)
                            if (frequencyHz > 0)
                                appSettings.sdrDecoderCenterFrequencyHz = frequencyHz
                            text = root.formatFrequencyKhz(
                                appSettings.sdrDecoderCenterFrequencyHz)
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Only this bounded RF region is sent to stream detection and CW decoding"
                    }
                    Label { text: "Decoder bandwidth" }
                    ComboBox {
                        objectName: "sdrDecoderBandwidthCombo"
                        Layout.fillWidth: true
                        model: [6000, 12000, 24000, 48000, 96000]
                        currentIndex: model.indexOf(appSettings.sdrDecoderBandwidthHz)
                        displayText: (appSettings.sdrDecoderBandwidthHz / 1000)
                                     + " kHz"
                        onActivated:
                            appSettings.sdrDecoderBandwidthHz = currentValue
                        ToolTip.visible: hovered
                        ToolTip.text: "Limits CPU-intensive CW detection while the full acquired spectrum remains visible"
                    }
                    Label { text: "SDR LO offset (Hz)" }
                    TextField {
                        objectName: "sdrRadioLoOffsetField"
                        Layout.fillWidth: true
                        enabled: appSettings.sdrFollowRadioVfo
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        text: appSettings.sdrRadioLoOffsetHz.toString()
                        validator: RegularExpressionValidator { regularExpression: /-?[0-9]{1,8}/ }
                        onEditingFinished: appSettings.sdrRadioLoOffsetHz = Number(text)
                        ToolTip.visible: hovered
                        ToolTip.text: "Optional offset between the radio RX frequency and SDR center; automatically bounded so the decoder remains inside the acquired passband"
                    }
                    Label { text: "Visible RF span" }
                    Label {
                        objectName: "sdrWidePassbandLabel"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#43c6ac"
                        text: appSettings.sdrWidePassbandSummary
                    }
                    Label { text: "Gain control" }
                    CheckBox {
                        objectName: "sdrAutomaticGainCheck"
                        text: "Use device automatic gain when supported"
                        enabled: appSettings.sdrBackendAvailable
                                 && appSettings.sdrAutomaticGainAvailable
                        checked: appSettings.sdrAutomaticGain
                        onToggled: appSettings.sdrAutomaticGain = checked
                    }
                    Label { text: "Manual gain (dB)" }
                    SpinBox {
                        objectName: "sdrGainSpinBox"
                        editable: true
                        from: Math.ceil(appSettings.sdrMinimumGainDb)
                        to: Math.floor(appSettings.sdrMaximumGainDb)
                        value: Math.round(appSettings.sdrGainDb)
                        enabled: appSettings.sdrBackendAvailable
                                 && !appSettings.sdrAutomaticGain
                        onValueModified: appSettings.sdrGainDb = value
                    }
                    Label { text: "Safety and availability" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#f3bd55"
                        text: appSettings.sdrBackendAvailable
                            ? "Configuration is receive-only. Selecting or refreshing a device does not start it; use the receiver workspace to begin reception."
                            : "Install SoapySDR plus the receiver-specific module (for example RTL-SDR or SDRplay), then use an SDR-enabled CW Buddy build. No external SDR application is required."
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
                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Optionally configure a compatible local decoding model. The deterministic decoder remains available and model output cannot control transmission."
                    }
                    Label { text: "Keying model" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RadioButton {
                            objectName: "keyingModelAdaptiveThresholdRadio"
                            text: "Adaptive threshold"
                            checked: appSettings.keyingModel !== "semi-markov"
                            onToggled: if (checked) appSettings.keyingModel = "adaptive-threshold"
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: 26
                            Layout.bottomMargin: 6
                            wrapMode: Text.WordWrap
                            color: "#91a0b1"
                            text: "Decides key-up and key-down from the envelope, moment by moment. Steadiest on hand and bug sending, and shows text soonest."
                        }
                        RadioButton {
                            objectName: "keyingModelSemiMarkovRadio"
                            text: "Semi-Markov (HSMM)"
                            checked: appSettings.keyingModel === "semi-markov"
                            onToggled: if (checked) appSettings.keyingModel = "semi-markov"
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: 26
                            wrapMode: Text.WordWrap
                            color: "#91a0b1"
                            text: "Weighs each mark and gap against the lengths Morse expects. Stronger on machine-sent, weighted and Farnsworth keying; weaker when the sender's timing wanders. Costs about one character of delay."
                        }
                    }
                    Label { text: "Weak signals" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        CheckBox {
                            objectName: "decodeWeakSignalsCheck"
                            text: "Decode every tracked signal"
                            checked: appSettings.decodeWeakSignals
                            onToggled: appSettings.decodeWeakSignals = checked
                        }
                        RowLayout {
                            spacing: 8
                            Label {
                                // The threshold decides nothing once every
                                // tracked signal is decoded, so it reads as
                                // inert rather than as a limit still in force.
                                color: appSettings.decodeWeakSignals ? "#667586" : "#c7d2df"
                                text: "Decode only above"
                            }
                            SpinBox {
                                objectName: "minimumDecodeSnrDbSpin"
                                enabled: !appSettings.decodeWeakSignals
                                // SpinBox counts in whole numbers, so the
                                // threshold is held here in tenths of a
                                // decibel and presented with one decimal.
                                from: 0
                                to: 400
                                stepSize: 1
                                editable: true
                                value: Math.round(appSettings.minimumDecodeSnrDb * 10)
                                validator: DoubleValidator {
                                    bottom: 0.0
                                    top: 40.0
                                    decimals: 1
                                    notation: DoubleValidator.StandardNotation
                                }
                                textFromValue: function(value, locale) {
                                    return Number(value / 10).toLocaleString(locale, 'f', 1)
                                }
                                valueFromText: function(text, locale) {
                                    return Math.round(Number.fromLocaleString(locale, text) * 10)
                                }
                                onValueModified: appSettings.minimumDecodeSnrDb = value / 10
                                ToolTip.visible: hovered && enabled
                                ToolTip.text: "Signal-to-noise level, in decibels, a tracked signal must reach before it is decoded"
                            }
                            Label {
                                color: appSettings.decodeWeakSignals ? "#667586" : "#91a0b1"
                                text: "dB above the noise floor"
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: "#91a0b1"
                            text: "A signal below the threshold is still detected, followed, and drawn in the spectrum; only its decoding is withheld. Off by default because below this level the decoder receives fragments rather than copy, filling the transcript with nothing while each such track costs a full decoder's work. The default of 12.0 dB is measured: the weakest track that carried a correctly recovered callsign across the capture corpus sat at 19.5 dB, leaving over seven decibels of margin. Enable the option above to decode every tracked signal regardless of level."
                        }
                    }
                    Label { text: "Local model" }
                    CheckBox {
                        objectName: "localDecoderEnabledCheck"
                        text: "Enable local model refinement"
                        checked: appSettings.localDecoderEnabled
                        enabled: appSettings.localDecoderBackendAvailable
                        onToggled: appSettings.localDecoderEnabled = checked
                    }
                    Label { text: "Model file" }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            objectName: "localDecoderModelPathField"
                            Layout.fillWidth: true
                            readOnly: true
                            text: appSettings.localDecoderModelPath
                            placeholderText: "No model selected"
                            ToolTip.visible: hovered && text.length > 0
                            ToolTip.text: text
                        }
                        Button {
                            objectName: "browseLocalDecoderModelButton"
                            text: "Browse…"
                            onClicked: localDecoderModelDialog.open()
                            ToolTip.visible: hovered
                            ToolTip.text: "Choose a compatible local ONNX model file"
                        }
                        Button {
                            text: "Clear"
                            enabled: appSettings.localDecoderModelPath.length > 0
                            onClicked: appSettings.clearLocalDecoderModel()
                            ToolTip.visible: hovered
                            ToolTip.text: "Remove the selected local model from this profile"
                        }
                    }
                    Label { text: "Metadata file" }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            objectName: "localDecoderMetadataPathField"
                            Layout.fillWidth: true
                            readOnly: true
                            text: appSettings.localDecoderMetadataPath
                            placeholderText: "No metadata selected"
                            ToolTip.visible: hovered && text.length > 0
                            ToolTip.text: text
                        }
                        Button {
                            objectName: "browseLocalDecoderMetadataButton"
                            text: "Browse…"
                            onClicked: localDecoderMetadataDialog.open()
                            ToolTip.visible: hovered
                            ToolTip.text: "Choose the JSON metadata describing the selected model"
                        }
                        Button {
                            text: "Clear"
                            enabled: appSettings.localDecoderMetadataPath.length > 0
                            onClicked: appSettings.clearLocalDecoderMetadata()
                            ToolTip.visible: hovered
                            ToolTip.text: "Remove the selected model metadata from this profile"
                        }
                    }
                    Label { text: "Status" }
                    Label {
                        objectName: "localDecoderStatusLabel"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: appSettings.localDecoderBackendAvailable
                              ? replayController.localCharacterStatus
                              : appSettings.localDecoderStatus
                        color: replayController.localCharacterState === "error"
                               ? "#ef7d85"
                               : (appSettings.localDecoderBackendAvailable
                                  ? "#43c6ac" : "#f3bd55")
                    }
                    Label { text: "Debug capture" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: "#91a0b1"
                            text: "Records raw live audio and per-track decoder internals to a timestamped folder, for troubleshooting a signal that will not decode. Review the files before sharing them: the audio is whatever the selected input picked up."
                        }
                        RowLayout {
                            spacing: 8
                            Button {
                                objectName: "settingsDebugCaptureButton"
                                text: replayController.debugCaptureActive
                                      ? "Stop capture" : "Start capture"
                                enabled: replayController.debugCaptureActive
                                         || replayController.liveCapturing
                                onClicked: replayController.debugCaptureActive
                                           ? replayController.stopDebugCapture()
                                           : replayController.startDebugCapture()
                                ToolTip.visible: hovered
                                ToolTip.text: replayController.debugCaptureActive
                                    ? "Stop and finalize the current diagnostic capture"
                                    : "Record bounded raw audio and decoder evidence for troubleshooting"
                            }
                            Button {
                                objectName: "settingsDebugCaptureFolderButton"
                                text: "Open capture folder"
                                // Nothing has been written yet before the first
                                // capture, so there is no folder to open.
                                enabled: replayController.debugCapturePath.length > 0
                                onClicked: replayController.openDebugCaptureFolder()
                                ToolTip.visible: hovered
                                ToolTip.text: enabled
                                    ? "Open the latest capture folder in the file manager"
                                    : "Create a diagnostic capture first"
                            }
                        }
                        RowLayout {
                            spacing: 8
                            Label { text: "Stop automatically after" }
                            SpinBox {
                                objectName: "debugCaptureMaximumSecondsSpin"
                                from: 30
                                to: 1800
                                stepSize: 30
                                editable: true
                                value: appSettings.debugCaptureMaximumSeconds
                                onValueModified: appSettings.debugCaptureMaximumSeconds = value
                            }
                            Label { text: "seconds"; color: "#91a0b1" }
                        }
                        Label {
                            objectName: "settingsDebugCaptureStatusLabel"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            visible: replayController.debugCaptureActive
                                     || replayController.debugCapturePath.length > 0
                            text: replayController.debugCaptureActive
                                  ? "Capturing... " + replayController.debugCaptureElapsedSeconds.toFixed(0)
                                    + "s / " + appSettings.debugCaptureMaximumSeconds + "s - "
                                    + replayController.debugCapturePath
                                  : "Last capture: " + replayController.debugCaptureNote
                                    + " - " + replayController.debugCapturePath
                            color: replayController.debugCaptureActive ? "#f3bd55" : "#6c7c8e"
                        }
                    }
                    Rectangle {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        height: 1
                        color: "#2b3541"
                    }
                    Label {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Optional offline callsign suggestions can use a managed Super Check Partial MASTER.SCP cache or an operator-supplied file. Matches only rank calls already present in multiple acoustic alternatives; they never confirm a stream, replace decoded text, alert on your call, or control transmission."
                    }
                    Label { text: "Managed SCP database" }
                    CheckBox {
                        objectName: "managedCallsignDatabaseEnabledCheck"
                        text: "Use managed offline cache"
                        checked: callsignDatabaseUpdater.managedEnabled
                        onToggled: callsignDatabaseUpdater.managedEnabled = checked
                    }
                    Label { text: "Managed updates" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        CheckBox {
                            objectName: "managedCallsignDatabaseAutoUpdateCheck"
                            text: "Automatically check at most daily"
                            enabled: callsignDatabaseUpdater.managedEnabled
                            checked: callsignDatabaseUpdater.autoUpdateEnabled
                            onToggled: callsignDatabaseUpdater.autoUpdateEnabled = checked
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Button {
                                objectName: "managedCallsignDatabaseUpdateButton"
                                enabled: callsignDatabaseUpdater.managedEnabled
                                         && !callsignDatabaseUpdater.checking
                                         && !callsignDatabaseUpdater.downloading
                                text: callsignDatabaseUpdater.checking
                                      || callsignDatabaseUpdater.downloading
                                      ? "Working…"
                                      : (callsignDatabaseUpdater.updateAvailable
                                         ? "Download update"
                                         : "Check for updates")
                                onClicked: callsignDatabaseUpdater.updateAvailable
                                           ? callsignDatabaseUpdater.updateDatabase()
                                           : callsignDatabaseUpdater.checkForUpdates()
                                ToolTip.visible: hovered
                                ToolTip.text: callsignDatabaseUpdater.updateAvailable
                                    ? "Download, validate, and atomically replace the managed offline list"
                                    : "Check the managed callsign-list provider for a newer release"
                            }
                            Label {
                                Layout.fillWidth: true
                                color: "#8290a0"
                                elide: Text.ElideRight
                                text: callsignDatabaseUpdater.installedVersion.length > 0
                                      ? "Installed release "
                                        + callsignDatabaseUpdater.installedVersion.substring(0, 10)
                                      : "No managed copy installed"
                            }
                        }
                        Label {
                            objectName: "managedCallsignDatabaseStatusLabel"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: "#80cbc4"
                            text: callsignDatabaseUpdater.statusMessage
                                  + " · Last check: "
                                  + callsignDatabaseUpdater.lastCheckedText
                        }
                    }
                    Label { text: "Local callsign suggestions" }
                    CheckBox {
                        objectName: "localCallsignDatabaseEnabledCheck"
                        text: "Enable operator-supplied local file"
                        enabled: !callsignDatabaseUpdater.managedEnabled
                        checked: appSettings.localCallsignDatabaseEnabled
                        onToggled: appSettings.localCallsignDatabaseEnabled = checked
                    }
                    Label { text: "Callsign-list file" }
                    RowLayout {
                        Layout.fillWidth: true
                        TextField {
                            objectName: "localCallsignDatabasePathField"
                            Layout.fillWidth: true
                            readOnly: true
                            text: appSettings.localCallsignDatabasePath
                            placeholderText: "No local callsign list selected"
                            ToolTip.visible: hovered && text.length > 0
                            ToolTip.text: text
                        }
                        Button {
                            objectName: "browseLocalCallsignDatabaseButton"
                            text: "Browse…"
                            enabled: !callsignDatabaseUpdater.managedEnabled
                            onClicked: localCallsignDatabaseDialog.open()
                            ToolTip.visible: hovered
                            ToolTip.text: "Choose an operator-supplied master.scp or Call History file"
                        }
                        Button {
                            text: "Clear"
                            enabled: !callsignDatabaseUpdater.managedEnabled
                                     && appSettings.localCallsignDatabasePath.length > 0
                            onClicked: appSettings.clearLocalCallsignDatabase()
                            ToolTip.visible: hovered
                            ToolTip.text: "Remove the operator-supplied callsign list from this profile"
                        }
                    }
                    Label { text: "Correct near misses" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        CheckBox {
                            objectName: "callsignDatabaseCorrectionCheck"
                            text: "Correct near-miss callsigns from the list"
                            // Requires a loaded list: with none there is
                            // nothing to correct against, and a control that
                            // silently does nothing is worse than one that
                            // says why it cannot act.
                            enabled: replayController.offlineCallsignDatabaseState === "ready"
                            checked: appSettings.callsignDatabaseCorrectionEnabled
                            onToggled: appSettings.callsignDatabaseCorrectionEnabled = checked
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: replayController.offlineCallsignDatabaseState === "ready"
                                   ? "#91a0b1" : "#f3bd55"
                            text: replayController.offlineCallsignDatabaseState === "ready"
                                  ? "When a decoded callsign is within two characters of a single entry in the list, suggest that entry instead. Off by default: two listed stations can differ by one character, so a correction can name a station that was never sent. The suggestion stays advisory either way and never changes the transcript or the confirmed callsign."
                                  : "Unavailable until a callsign list is loaded. Enable the managed list above, or select an operator-supplied file, and this becomes available once its state reads ready."
                        }
                    }

                    Label { text: "Local-list state" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Label {
                            objectName: "localCallsignDatabaseStatusLabel"
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: replayController.offlineCallsignDatabaseStatus
                            color: replayController.offlineCallsignDatabaseState
                                   === "error" ? "#ef7d85"
                                   : (replayController.offlineCallsignDatabaseState
                                      === "ready" ? "#80cbc4" : "#8290a0")
                        }
                        Button {
                            objectName: "reloadLocalCallsignDatabaseButton"
                            text: "Reload local file"
                            enabled: appSettings.localCallsignDatabaseEnabled
                                     && !callsignDatabaseUpdater.managedEnabled
                                     && appSettings.localCallsignDatabasePath.length > 0
                            onClicked: appSettings.reloadLocalCallsignDatabase()
                            ToolTip.visible: hovered
                            ToolTip.text: enabled
                                ? "Reload and validate the selected local callsign file"
                                : "Enable and select an operator-supplied file first"
                        }
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
                        Button {
                            text: "Refresh"; onClicked: appSettings.refreshDetectedRadios()
                            ToolTip.visible: hovered
                            ToolTip.text: "Refresh positively identified radios from configured integrations"
                        }
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
                    Label { text: "RX tuning step" }
                    LabeledSlider {
                        objectName: "radioTuningStepSlider"
                        Layout.fillWidth: true
                        caption: "kHz"
                        from: 1
                        to: 100
                        stepSize: 1
                        decimals: 0
                        value: appSettings.radioTuningStepHz / 1000
                        onMoved: value => appSettings.radioTuningStepHz = Math.round(value * 1000)
                    }
                    Label { text: "OmniRig radio slot"; visible: appSettings.frequencyBackendIndex === 0 }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: appSettings.frequencyBackendIndex === 0
                        SpinBox { from: 1; to: 2; value: appSettings.omniRigSlot; onValueModified: appSettings.omniRigSlot = value }
                        Button {
                            text: "Configure OmniRig"
                            enabled: appSettings.omniRigAvailable
                            onClicked: appSettings.showOmniRigConfiguration()
                            ToolTip.visible: hovered
                            ToolTip.text: enabled
                                ? "Open OmniRig to configure its radio model, COM port, baud rate, parity, and stop bits"
                                : "OmniRig is not available on this system"
                        }
                    }
                    Label { text: "Hamlib rigctld host"; visible: appSettings.frequencyBackendIndex === 1 }
                    TextField { Layout.fillWidth: true; visible: appSettings.frequencyBackendIndex === 1; text: appSettings.hamlibHost; placeholderText: "127.0.0.1"; onEditingFinished: appSettings.hamlibHost = text }
                    Label { text: "Hamlib rigctld port"; visible: appSettings.frequencyBackendIndex === 1 }
                    SpinBox { visible: appSettings.frequencyBackendIndex === 1; editable: true; from: 1; to: 65535; value: appSettings.hamlibPort; onValueModified: appSettings.hamlibPort = value }
                    Label { text: "Hamlib VFO mapping"; visible: appSettings.frequencyBackendIndex === 1 }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: appSettings.frequencyBackendIndex === 1
                        TextField { Layout.fillWidth: true; text: appSettings.hamlibRxVfo; placeholderText: "VFOA (RX)"; onEditingFinished: appSettings.hamlibRxVfo = text }
                        TextField { Layout.fillWidth: true; text: appSettings.hamlibTxVfo; placeholderText: "VFOB (TX)"; onEditingFinished: appSettings.hamlibTxVfo = text }
                    }
                    Label { text: "Hamlib control"; visible: appSettings.frequencyBackendIndex === 1 }
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: appSettings.frequencyBackendIndex === 1
                        CheckBox {
                            text: "Allow frequency, mode, and split writes"
                            checked: appSettings.hamlibWritable
                            onToggled: appSettings.hamlibWritable = checked
                        }
                        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#91a0b1"; text: appSettings.hamlibState }
                        RowLayout {
                            Button { text: "Connect"; onClicked: appSettings.connectHamlib(); ToolTip.visible: hovered; ToolTip.text: "Connect to the configured rigctld endpoint; PTT and KEY are never exposed through this provider" }
                            Button { text: "Disconnect"; onClicked: appSettings.disconnectHamlib(); ToolTip.visible: hovered; ToolTip.text: "Close the Hamlib radio-control connection" }
                        }
                    }
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
                            Button {
                                text: "Test read-only"; onClicked: appSettings.testCat4omConnection()
                                ToolTip.visible: hovered
                                ToolTip.text: "Connect as an observer without requesting radio-control ownership"
                            }
                            Button {
                                text: "Connect control"; onClicked: appSettings.connectCat4omControl()
                                ToolTip.visible: hovered
                                ToolTip.text: "Connect and negotiate the configured control capability"
                            }
                            Button {
                                text: "Request ownership"; enabled: !appSettings.cat4omCanWrite
                                onClicked: appSettings.requestCat4omOwnership()
                                ToolTip.visible: hovered
                                ToolTip.text: enabled
                                    ? "Request the service's exclusive radio-control lease"
                                    : "This connection already has write capability"
                            }
                            Button {
                                text: "Disconnect"; onClicked: appSettings.disconnectCat4om()
                                ToolTip.visible: hovered
                                ToolTip.text: "Close the CAT4OM connection and release its control state"
                            }
                        }
                    }
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
                        Button {
                            text: "Restore radio defaults"; onClicked: appSettings.resetToReferenceDefaults()
                            ToolTip.visible: hovered
                            ToolTip.text: "Restore the selected reference rig's editable CAT defaults"
                        }
                    }
                    Label { text: "" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: appSettings.radioEnabled
                              ? (appSettings.frequencyBackendIndex === 0
                                 ? "OmniRig owns radio-model and serial framing configuration; CW Buddy selects only the OmniRig slot. Direct key/PTT remains an independent connection."
                                 : appSettings.frequencyBackendIndex === 1
                                   ? "rigctld owns the physical radio and serial framing. CW Buddy configures only its loopback endpoint, VFO mapping, and write permission."
                                   : "CAT4OM owns the physical radio and serial framing. CW Buddy configures only its Control service connection; passwords are never saved.")
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
                    Label { text: "Hardware keying" }
                    CheckBox {
                        text: "Enable direct RTS/DTR keying"
                        checked: appSettings.directKeyingEnabled
                        enabled: appSettings.radioEnabled
                        onToggled: appSettings.directKeyingEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: enabled
                            ? "Opt in to the dedicated local serial KEY/PTT adapter; every reconnect starts disarmed"
                            : "Enable a radio profile before configuring transmit hardware"
                    }
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
                    Label { text: "Hardware validation" }
                    ColumnLayout {
                        Layout.fillWidth: true
                        enabled: appSettings.directKeyingEnabled
                                 && appSettings.keyingPort.length > 0
                        CheckBox {
                            id: radioDisconnectedForLoopback
                            objectName: "radioDisconnectedForLoopbackCheck"
                            text: "Radio is physically disconnected; RTS→CTS and DTR→DSR loopbacks are fitted"
                        }
                        Button {
                            objectName: "runDirectKeyingLoopbackButton"
                            text: "Run measured loopback"
                            enabled: radioDisconnectedForLoopback.checked
                            onClicked: {
                                appSettings.runDirectKeyingLoopback(
                                    radioDisconnectedForLoopback.checked)
                                radioDisconnectedForLoopback.checked = false
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: "Test the exact selected port electrically; outputs are released and the port is closed on every result"
                        }
                        Label {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: appSettings.directKeyingValidated
                                   ? "#43c6ac" : "#f3bd55"
                            text: appSettings.directKeyingAcceptanceStatus
                        }
                    }
                    Label { text: "TX speed" }
                    ComboBox {
                        model: ["Match selected RX", "Fixed"]
                        currentIndex: appSettings.txSpeedMode
                        onActivated: appSettings.txSpeedMode = currentIndex
                        ToolTip.visible: hovered
                        ToolTip.text: currentIndex === 0
                            ? "Snapshot a supported selected-stream WPM when preparing text; otherwise use the fixed fallback"
                            : "Always use the configured fixed transmit speed"
                    }
                    Label { text: "Fixed/fallback TX speed" }
                    LabeledSlider {
                        Layout.fillWidth: true
                        caption: "WPM"
                        from: 5
                        to: 80
                        stepSize: 1
                        value: appSettings.fixedTxWpm
                        onMoved: value => appSettings.fixedTxWpm = Math.round(value)
                    }
                    Label { text: "Quick TX macros" }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        TextField { Layout.fillWidth: true; maximumLength: 64; text: appSettings.txMacro1; placeholderText: "Macro 1"; onEditingFinished: appSettings.txMacro1 = text }
                        TextField { Layout.fillWidth: true; maximumLength: 64; text: appSettings.txMacro2; placeholderText: "Macro 2"; onEditingFinished: appSettings.txMacro2 = text }
                        TextField { Layout.fillWidth: true; maximumLength: 64; text: appSettings.txMacro3; placeholderText: "Macro 3"; onEditingFinished: appSettings.txMacro3 = text }
                        TextField { Layout.fillWidth: true; maximumLength: 64; text: appSettings.txMacro4; placeholderText: "Macro 4"; onEditingFinished: appSettings.txMacro4 = text }
                    }
                    Label { text: "" }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#f3bd55"
                        text: "Ports are enumerated without opening them. The first hardware slice accepts distinct RTS/DTR lines with active-high interfaces only. Opening or changing the adapter always drives KEY then PTT inactive and leaves transmission disarmed. Validate with the radio disconnected, then a physical loopback, before using a dummy load."
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
                    Label { text: "Spectrum view" }
                    ComboBox {
                        model: ["Audio spectrum", "CW symbols"]
                        currentIndex: appSettings.spectrumDisplayMode
                        onActivated: appSettings.spectrumDisplayMode = currentIndex
                    }
                    Label { text: "Target FPS" }
                    LabeledSlider { Layout.fillWidth: true; caption: "frames/s"; from: 10; to: 120; value: appSettings.targetFps; onMoved: value => appSettings.targetFps = Math.round(value) }
                    Label { text: "Waterfall lines / second" }
                    LabeledSlider { Layout.fillWidth: true; caption: "rows/s"; from: 1; to: 120; value: appSettings.waterfallRate; onMoved: value => appSettings.waterfallRate = Math.round(value) }
                    Label { text: "Waterfall history (seconds)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "seconds"; from: 5; to: 30; value: appSettings.waterfallTimeSpanSeconds; onMoved: value => appSettings.waterfallTimeSpanSeconds = Math.round(value) }
                    Label { text: "Visual CW reference" }
                    CheckBox { text: "Show red visual boundaries"; checked: appSettings.showCwGuide; onToggled: appSettings.showCwGuide = checked }
                    Label { text: "Visual center tone (Hz)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "Hz"; from: 0; to: appSettings.audioAutomaticBandwidth ? 3000 : Math.max(3000, appSettings.audioUpperFrequencyHz); stepSize: 10; value: appSettings.cwGuideCenterHz; enabled: appSettings.showCwGuide; onMoved: value => appSettings.cwGuideCenterHz = value }
                    Label { text: "Visual width (Hz)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "Hz"; from: 10; to: 5000; stepSize: 10; value: appSettings.cwGuideWidthHz; enabled: appSettings.showCwGuide; onMoved: value => appSettings.cwGuideWidthHz = value }
                    Label { text: "Display level range" }
                    CheckBox { text: "Automatic display scaling"; checked: appSettings.automaticRange; onToggled: appSettings.automaticRange = checked }
                    Label { text: "Lower bound (dB)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "dBFS"; from: -200; to: 40; value: appSettings.lowerBoundDb; enabled: !appSettings.automaticRange; onMoved: value => appSettings.lowerBoundDb = value }
                    Label { text: "Upper bound (dB)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "dBFS"; from: -190; to: 50; value: appSettings.upperBoundDb; enabled: !appSettings.automaticRange; onMoved: value => appSettings.upperBoundDb = value }
                    Label { text: "Automatic span (dB)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "dB"; from: 30; to: 100; value: appSettings.automaticRangeSpanDb; enabled: appSettings.automaticRange; onMoved: value => appSettings.automaticRangeSpanDb = value }
                    Label { text: "Waterfall noise suppression" }
                    CheckBox { text: "Darken bins near the measured noise floor"; checked: appSettings.waterfallNoiseSuppression; onToggled: appSettings.waterfallNoiseSuppression = checked }
                    Label { text: "Noise margin (dB)" }
                    LabeledSlider { Layout.fillWidth: true; caption: "dB"; from: 0; to: 30; value: appSettings.waterfallNoiseMarginDb; enabled: appSettings.waterfallNoiseSuppression && appSettings.spectrumDisplayMode === 0; onMoved: value => appSettings.waterfallNoiseMarginDb = value }
                    Label { text: "Spectrum averaging" }
                    LabeledSlider { Layout.fillWidth: true; caption: "frames"; from: 1; to: 32; value: appSettings.averagingFrames; onMoved: value => appSettings.averagingFrames = Math.round(value) }
                    Label { text: "Reference grid" }
                    CheckBox { text: "Show frequency and level grid"; checked: appSettings.showGrid; onToggled: appSettings.showGrid = checked }
                    Label { text: "Spectrum help" }
                    CheckBox {
                        objectName: "showSpectrumGestureHintsCheck"
                        text: "Show brief spectrum gesture hints"
                        checked: appSettings.showSpectrumGestureHints
                        onToggled: appSettings.showSpectrumGestureHints = checked
                        ToolTip.visible: hovered
                        ToolTip.text: "Show the spectrum pointer legend for at most 10 seconds, no more than once every five minutes"
                    }
                    Label { text: "Decoded signal timeout" }
                    LabeledSlider { Layout.fillWidth: true; caption: "seconds"; from: 5; to: 300; value: appSettings.decodedSignalTimeoutSeconds; onMoved: value => appSettings.decodedSignalTimeoutSeconds = Math.round(value) }
                    Label { text: "" }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: "#91a0b1"; text: "Waterfall history is a constant time window: resizing, startup fill, and line density do not stretch or collapse Morse timing. Automatic display scaling uses a stable minimum span so receiver noise stays dark instead of pumping through the palette. Noise suppression affects waterfall colors only; raw spectrum bins remain available to the future decoder. The same operational controls are available directly below the spectrum. A decoded signal's marker and session remain available for the configured timeout after it goes silent, then are removed. Its frequency keeps the same reserved color for at least five minutes so a later pass is visually recognizable." }
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
                        text: "Saved per station profile. An exact stable decode highlights your callsign and flashes its open decoder card. The value also populates station logging fields; any future closing macro remains separately guarded."
                    }
                    Label { text: "Operating role"; font.weight: Font.DemiBold }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        RadioButton {
                            objectName: "operatorRoleMonitorRadio"
                            text: "Monitoring"
                            checked: appSettings.operatorRole !== "search-and-pounce"
                                     && appSettings.operatorRole !== "runner"
                            onToggled: if (checked) appSettings.operatorRole = "monitor"
                        }
                        RadioButton {
                            objectName: "operatorRoleSearchAndPounceRadio"
                            text: "Search and pounce"
                            checked: appSettings.operatorRole === "search-and-pounce"
                            onToggled: if (checked) appSettings.operatorRole = "search-and-pounce"
                        }
                        RadioButton {
                            objectName: "operatorRoleRunnerRadio"
                            text: "Running"
                            checked: appSettings.operatorRole === "runner"
                            onToggled: if (checked) appSettings.operatorRole = "runner"
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: 4
                            wrapMode: Text.WordWrap
                            color: "#91a0b1"
                            text: "Which station a stream is expected to carry. Exchange context alone cannot always tell: TU precedes a runner identifying itself and equally the station it has just worked. Hunting, the stream you are listening to is a runner, so its own call is the label; running, it is somebody answering you. Monitoring makes no assumption. Your own callsign never labels another station's stream in any of them."
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#91a0b1"
                        text: "Open Profiles from the main toolbar to create or select another station. For dedicated shortcuts or services, launch with --profile \"name\". Separate processes can use separate profiles and radios."
                    }
                    Button {
                        text: "Run setup helper again"; onClicked: root.setupRequested()
                        ToolTip.visible: hovered
                        ToolTip.text: "Open the guided station-profile setup without transmitting"
                    }
                }
            }

            ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    anchors.margins: 22
                    spacing: 14
                    Label { text: "About CW Buddy"; font.pixelSize: 22; font.weight: Font.DemiBold }
                    Label {
                        objectName: "aboutVersionLabel"
                        text: "Version " + Qt.application.version
                        color: "#91a0b1"
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2b3541" }
                    Label { text: "Updates"; font.pixelSize: 17; font.weight: Font.DemiBold }
                    CheckBox {
                        objectName: "autoUpdateCheckToggle"
                        text: "Automatically check for updates"
                        checked: updateChecker.autoCheckEnabled
                        onToggled: updateChecker.autoCheckEnabled = checked
                    }
                    RowLayout {
                        spacing: 10
                        Button {
                            objectName: "checkForUpdatesButton"
                            text: updateChecker.checking ? "Checking…" : "Check for updates"
                            enabled: !updateChecker.checking
                            onClicked: updateChecker.checkForUpdates()
                            ToolTip.visible: hovered
                            ToolTip.text: "Check the published release manifest for a newer application version"
                        }
                        Label {
                            text: "Last checked: " + updateChecker.lastCheckedText
                            color: "#6c7c8e"
                            font.pixelSize: 11
                        }
                    }
                    Label {
                        objectName: "updateStatusLabel"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: updateChecker.statusMessage
                        color: updateChecker.updateAvailable ? "#4dff88" : "#91a0b1"
                    }
                    RowLayout {
                        objectName: "updateActionRow"
                        visible: updateChecker.updateActionVisible
                        spacing: 10
                        Button {
                            objectName: "downloadUpdateButton"
                            visible: updateChecker.downloadActionVisible
                            text: updateChecker.downloading
                                  ? "Downloading… " + Math.round(updateChecker.downloadProgress * 100) + "%"
                                  : "Download update"
                            enabled: !updateChecker.downloading
                            onClicked: updateChecker.downloadUpdate()
                            ToolTip.visible: hovered
                            ToolTip.text: "Download this platform package and verify its SHA-256 checksum"
                        }
                        Button {
                            objectName: "openUpdateButton"
                            visible: updateChecker.verifiedDownloadActionsVisible
                            text: "Open Installer"
                            onClicked: updateChecker.openDownloadedFile()
                            ToolTip.visible: hovered
                            ToolTip.text: "Open the verified package with the operating-system installer"
                        }
                        Button {
                            objectName: "revealUpdateButton"
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
                    }
                    Label {
                        visible: updateChecker.updateAvailable && !updateChecker.platformSupported
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: "#f3bd55"
                        text: "Guided downloads are not available for this platform yet; visit the release page instead."
                        font.pixelSize: 11
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#2b3541" }
                    Label { text: "Author"; color: "#8290a0" }
                    Label { text: "Alessio Bravi (IU0LFQ / AD2FC)"; font.pixelSize: 17; font.weight: Font.Medium }
                    Label { text: "Author Website"; color: "#8290a0" }
                    Button {
                        text: "https://iu0lfq.it/"
                        flat: true
                        onClicked: Qt.openUrlExternally("https://iu0lfq.it/")
                        ToolTip.visible: hovered
                        ToolTip.text: "Open the author website in your default browser"
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
            Button {
                text: "Apply"; highlighted: true; onClicked: appSettings.apply()
                ToolTip.visible: hovered
                ToolTip.text: "Validate and save all settings in this profile"
            }
        }
    }

    FileDialog {
        id: localDecoderModelDialog
        objectName: "localDecoderModelDialog"
        title: "Select local decoder model"
        fileMode: FileDialog.OpenFile
        nameFilters: ["ONNX models (*.onnx)", "All files (*)"]
        onAccepted: appSettings.selectLocalDecoderModel(selectedFile)
    }

    FileDialog {
        id: localDecoderMetadataDialog
        objectName: "localDecoderMetadataDialog"
        title: "Select local decoder metadata"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON metadata (*.json)", "All files (*)"]
        onAccepted: appSettings.selectLocalDecoderMetadata(selectedFile)
    }

    FileDialog {
        id: localCallsignDatabaseDialog
        objectName: "localCallsignDatabaseDialog"
        title: "Select local callsign list"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Callsign lists (master.scp *.scp *.txt *.csv)",
                      "All files (*)"]
        onAccepted: appSettings.selectLocalCallsignDatabase(selectedFile)
    }
}
