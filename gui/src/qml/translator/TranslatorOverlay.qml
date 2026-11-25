// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// In-viewport translator UI. Deliberately a child of the stream view rather
// than a separate window so Gamescope keeps the game focused.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import org.streetpea.chiaking

Item {
    id: root

    anchors.fill: parent
    z: 900
    focus: false

    property bool settingsOpen: false
    property bool dictionaryOpen: false
    property bool autoHidden: false
    property string captureAction: ""
    readonly property bool menuOpen: settingsOpen || Translator.roiSelectionActive || Translator.backlogVisible || dictionaryOpen
    readonly property var videoViewportRect: Translator.videoViewport(16.0 / 9.0,
        height > 0 ? width / height : 16.0 / 9.0,
        Chiaki.window ? Chiaki.window.videoMode : 0,
        Chiaki.window ? Chiaki.window.ZoomFactor : -1)

    function saveHotkeys() {
        const ok = Translator.setShortcuts(shortcutEnableField.text, shortcutTranslateField.text,
            shortcutOverlayField.text, shortcutBacklogField.text, shortcutSettingsField.text)
        if (ok) {
            shortcutEnableField.text = Translator.shortcutEnable
            shortcutTranslateField.text = Translator.shortcutTranslate
            shortcutOverlayField.text = Translator.shortcutOverlay
            shortcutBacklogField.text = Translator.shortcutBacklog
            shortcutSettingsField.text = Translator.shortcutSettings
        }
        return ok
    }

    function assignCapturedShortcut(sequence) {
        if (captureAction === "enable")
            shortcutEnableField.text = sequence
        else if (captureAction === "translate")
            shortcutTranslateField.text = sequence
        else if (captureAction === "overlay")
            shortcutOverlayField.text = sequence
        else if (captureAction === "backlog")
            shortcutBacklogField.text = sequence
        else if (captureAction === "settings")
            shortcutSettingsField.text = sequence
        captureAction = ""
        saveHotkeys()
    }

    function clearCapturedShortcut() {
        if (captureAction === "enable")
            shortcutEnableField.text = ""
        else if (captureAction === "translate")
            shortcutTranslateField.text = ""
        else if (captureAction === "overlay")
            shortcutOverlayField.text = ""
        else if (captureAction === "backlog")
            shortcutBacklogField.text = ""
        else if (captureAction === "settings")
            shortcutSettingsField.text = ""
        captureAction = ""
        saveHotkeys()
    }

    onMenuOpenChanged: {
        if (menuOpen)
            Chiaki.window.grabInput()
        else
            Chiaki.window.releaseInput()
    }

    Component.onDestruction: {
        if (menuOpen && Chiaki.window)
            Chiaki.window.releaseInput()
        if (Chiaki.window)
            Chiaki.window.setTranslatorTouchRegion(0, 0, 0, 0, false)
    }

    function openDictionaryIfNeeded() {
        dictionaryOpen = Translator.dictionaryEntries.length > 0
    }

    function updateSubtitleTouchRegion() {
        if (!Chiaki.window)
            return
        if (Translator.overlayRegions.length > 0) {
            let minX = 1.0, minY = 1.0, maxX = 0.0, maxY = 0.0
            for (let index = 0; index < Translator.overlayRegions.length; ++index) {
                const region = Translator.overlayRegions[index]
                minX = Math.min(minX, Number(region.x))
                minY = Math.min(minY, Number(region.y))
                maxX = Math.max(maxX, Number(region.x) + Number(region.width))
                maxY = Math.max(maxY, Number(region.y) + Number(region.height))
            }
            const viewport = root.videoViewportRect
            Chiaki.window.setTranslatorTouchRegion(
                (viewport.x + minX * viewport.width) * width,
                (viewport.y + minY * viewport.height) * height,
                (maxX - minX) * viewport.width * width,
                (maxY - minY) * viewport.height * height,
                Translator.enabled && Translator.overlayVisible)
            return
        }
        Chiaki.window.setTranslatorTouchRegion(
            subtitle.x, subtitle.y, subtitle.width, subtitle.height,
            subtitle.visible && Translator.enabled && Translator.overlayVisible)
    }

    Connections {
        target: Translator
        function onDictionaryEntriesChanged() {
            root.dictionaryOpen = Translator.dictionaryEntries.length > 0
        }
    }

    Connections {
        target: Translator
        function onSettingsRequested() { root.settingsOpen = true }
        function onOverlayTextChanged() {
            if (Translator.overlayAutoHideSeconds <= 0)
                return
            root.autoHidden = false
            overlayHideTimer.restart()
        }
        function onOverlayAutoHideSecondsChanged() {
            if (Translator.overlayAutoHideSeconds <= 0)
                overlayHideTimer.stop()
            else if (Translator.overlayVisible && Translator.overlayText.length > 0)
                overlayHideTimer.restart()
        }
        function onOverlayRegionsChanged() { root.updateSubtitleTouchRegion() }
        function onOverlayVisibleChanged() {
            if (Translator.overlayVisible) {
                root.autoHidden = false
                if (Translator.overlayAutoHideSeconds > 0 && Translator.overlayText.length > 0)
                    overlayHideTimer.restart()
            } else {
                overlayHideTimer.stop()
                root.autoHidden = false
            }
        }
    }

    Timer {
        id: overlayHideTimer
        interval: Math.max(1, Translator.overlayAutoHideSeconds * 1000)
        repeat: false
        onTriggered: {
            if (Translator.enabled && Translator.overlayVisible && Translator.overlayText.length > 0)
                root.autoHidden = true
        }
    }

    Connections {
        target: Chiaki
        function onSessionChanged() {
            if (Chiaki.session && Translator.enabled)
                Translator.prepare()
            if (!Chiaki.session) {
                root.settingsOpen = false
                root.dictionaryOpen = false
                Translator.roiSelectionActive = false
                Translator.backlogVisible = false
            }
        }
    }

    Component.onCompleted: {
        if (Translator.enabled)
            Translator.prepare()
        updateSubtitleTouchRegion()
    }

    Shortcut {
        sequence: Translator.shortcutEnable
        enabled: Translator.shortcutEnable.length > 0
        onActivated: Translator.toggleEnabled()
    }
    Shortcut {
        sequence: Translator.shortcutTranslate
        enabled: Translator.shortcutTranslate.length > 0
        onActivated: Translator.manualTranslate()
    }
    Shortcut {
        sequence: Translator.shortcutOverlay
        enabled: Translator.shortcutOverlay.length > 0
        onActivated: Translator.toggleOverlay()
    }
    Shortcut {
        sequence: Translator.shortcutBacklog
        enabled: Translator.shortcutBacklog.length > 0
        onActivated: Translator.toggleBacklog()
    }
    Shortcut {
        sequence: Translator.shortcutSettings
        enabled: Translator.shortcutSettings.length > 0
        onActivated: root.settingsOpen = true
    }

    // Small always-available entry point. Kept away from the centre so it does
    // not interfere with gameplay input.
    Rectangle {
        id: gear
        width: 38
        height: 38
        radius: 19
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        color: settingsOpen ? "#e6007acc" : "#99000000"
        border.color: "#66ffffff"
        border.width: 1
        visible: Chiaki.session !== null

        Text {
            anchors.centerIn: parent
            text: "訳"
            color: "white"
            font.pixelSize: 18
            font.bold: true
        }

        TapHandler {
            onTapped: {
                root.settingsOpen = !root.settingsOpen
                if (root.settingsOpen)
                    Translator.prepare()
            }
        }
    }

    // Quick real-time enable/disable. Tapping it starts or stops the pipeline.
    Rectangle {
        id: liveBadge
        anchors.top: gear.bottom
        anchors.right: parent.right
        anchors.topMargin: 6
        anchors.rightMargin: 12
        width: 58
        height: 24
        radius: 12
        color: Translator.enabled ? "#cc00875a" : "#99000000"
        border.color: "#66ffffff"
        border.width: 1
        visible: Chiaki.session !== null

        Text {
            anchors.centerIn: parent
            text: Translator.enabled ? "LIVE" : "OFF"
            color: "white"
            font.pixelSize: 11
            font.bold: true
        }

        TapHandler {
            onTapped: Translator.toggleEnabled()
        }
    }

    // Status line: OCR timing, duplicate suppression, backend errors.
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 12
        width: Math.min(statusText.implicitWidth + 20, parent.width - 80)
        height: visible ? 26 : 0
        radius: 6
        color: "#99000000"
        visible: Chiaki.session !== null && Translator.statusText.length > 0 &&
                 (Translator.enabled || Translator.prepared || root.settingsOpen)

        Text {
            id: statusText
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            elide: Text.ElideRight
            width: parent.width - 20
            text: Translator.statusText
            color: "#d0ffffff"
            font.pixelSize: 11
        }
    }

    // Box-accurate dialogue blocks from the OCR regions.
    Repeater {
        model: Translator.overlayRegions
        delegate: Rectangle {
            id: regionBlock
            readonly property real blockX: (root.videoViewportRect.x +
                Number(modelData.x) * root.videoViewportRect.width) * parent.width
            readonly property real blockY: (root.videoViewportRect.y +
                Number(modelData.y) * root.videoViewportRect.height) * parent.height
            readonly property real blockW: Math.max(120, Math.min(parent.width - 8,
                Number(modelData.width) * root.videoViewportRect.width * parent.width))
            readonly property real blockH: Math.max(44, Math.min(parent.height - 8,
                Number(modelData.height) * root.videoViewportRect.height * parent.height + 8))
            x: Math.max(4, Math.min(parent.width - blockW - 4, blockX))
            y: Math.max(4, Math.min(parent.height - blockH - 4, blockY))
            width: blockW
            height: blockH
            radius: 8
            color: "#cc0b0c10"
            border.color: "#33ffffff"
            border.width: 1
            visible: Chiaki.session !== null && Translator.enabled && Translator.overlayVisible &&
                     !root.autoHidden && Translator.overlayRegions.length > 0

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                Text {
                    width: parent.width
                    text: modelData.source
                    color: "#a8ffffff"
                    font.family: Translator.overlayFontFamily
                    font.pixelSize: Math.max(10, Math.min(14, regionBlock.height / 4))
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    width: parent.width
                    height: parent.height - 20
                    text: modelData.translation
                    color: "#fff4e6c8"
                    font.family: Translator.overlayFontFamily
                    font.pixelSize: Translator.overlayFontSize > 0 ? Translator.overlayFontSize
                        : Math.max(14, Math.min(28, width / 22))
                    font.bold: true
                    wrapMode: Text.Wrap
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TapHandler {
                onTapped: Translator.lookupWord(modelData.source)
            }
        }
    }

    // Translated subtitle. The source line is tappable for dictionary lookup.
    Rectangle {
        id: subtitle
        readonly property bool inPlace: Translator.overlayPlacement === "inplace"
        readonly property real roiX: (root.videoViewportRect.x +
            Translator.roiNormalized.x * root.videoViewportRect.width) * parent.width
        readonly property real roiY: (root.videoViewportRect.y +
            Translator.roiNormalized.y * root.videoViewportRect.height) * parent.height
        readonly property real roiW: Translator.roiNormalized.width *
            root.videoViewportRect.width * parent.width
        x: inPlace ? Math.max(4, Math.min(parent.width - width - 4, roiX)) : (parent.width - width) / 2
        y: inPlace ? Math.max(4, Math.min(parent.height - height - 4, roiY)) : parent.height - height - 28
        width: inPlace ? Math.max(120, Math.min(parent.width - 8, roiW)) : Math.min(parent.width * 0.88, 980)
        height: subtitleColumn.implicitHeight + 24
        radius: 10
        color: "#cc0b0c10"
        border.color: "#33ffffff"
        border.width: 1
        visible: Chiaki.session !== null && Translator.enabled && Translator.overlayVisible &&
                 !root.autoHidden && Translator.overlayRegions.length === 0 &&
                 Translator.overlayText.length > 0

        onVisibleChanged: root.updateSubtitleTouchRegion()
        onXChanged: root.updateSubtitleTouchRegion()
        onYChanged: root.updateSubtitleTouchRegion()
        onWidthChanged: root.updateSubtitleTouchRegion()
        onHeightChanged: root.updateSubtitleTouchRegion()

        Column {
            id: subtitleColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.margins: 14
            spacing: 6

            TextEdit {
                id: sourceEdit
                width: parent.width - 28
                x: 14
                text: Translator.sourceText
                color: "#a8ffffff"
                font.family: Translator.overlayFontFamily
                font.pixelSize: 13
                wrapMode: TextEdit.Wrap
                readOnly: true
                selectByMouse: false
                activeFocusOnPress: false
                cursorVisible: false
            }

            Item {
                width: 1
                height: 1
            }

            Text {
                x: 14
                width: parent.width - 28
                text: Translator.overlayText
                color: "#fff4e6c8"
                font.family: Translator.overlayFontFamily
                font.pixelSize: Translator.overlayFontSize > 0 ? Translator.overlayFontSize
                    : Math.max(18, Math.min(28, width / 26))
                font.bold: true
                wrapMode: Text.Wrap
            }
        }

        MouseArea {
            anchors.fill: parent
            propagateComposedEvents: false
            onClicked: function(mouse) {
                const origin = sourceEdit.mapToItem(subtitle, 0, 0)
                const pos = sourceEdit.positionAt(mouse.x - origin.x, mouse.y - origin.y)
                if (pos >= 0) {
                    Translator.lookupAt(sourceEdit.text, pos)
                    root.dictionaryOpen = true
                }
            }
        }
    }

    // Dictionary card for tapped words.
    Rectangle {
        id: dictionaryPanel
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16
        width: Math.min(parent.width * 0.6, 520)
        height: Math.min(dictionaryColumn.implicitHeight + 24, parent.height * 0.7)
        radius: 10
        color: "#ee111318"
        border.color: "#55ffffff"
        border.width: 1
        visible: root.dictionaryOpen && Translator.dictionaryEntries.length > 0

        Column {
            id: dictionaryColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 6

            Text {
                width: parent.width - 24
                x: 12
                text: Translator.dictionaryWord
                color: "white"
                font.pixelSize: 20
                font.bold: true
            }

            ListView {
                id: dictionaryList
                width: parent.width - 24
                x: 12
                height: Math.min(contentHeight, 240)
                clip: true
                model: Translator.dictionaryEntries
                spacing: 8

                delegate: Column {
                    width: dictionaryList.width
                    spacing: 2

                    Text {
                        text: modelData.surface + (modelData.reading.length > 0 ? "  【" + modelData.reading + "】" : "")
                        color: "#ffe8c46a"
                        font.pixelSize: 16
                        width: parent.width
                        wrapMode: Text.Wrap
                    }

                    Repeater {
                        model: modelData.glosses
                        delegate: Text {
                            text: (modelData.pos.length > 0 ? modelData.pos + ": " : "") + modelData.text
                            color: "#e8ffffff"
                            font.pixelSize: 13
                            width: parent.width
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }

    }

    // Dialogue backlog (L5).
    Rectangle {
        id: backlogPanel
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 54
        anchors.bottomMargin: 16
        anchors.rightMargin: 12
        width: Math.min(parent.width * 0.34, 460)
        radius: 10
        color: "#ee0d0f13"
        border.color: "#44ffffff"
        border.width: 1
        visible: Translator.backlogVisible

        Column {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Row {
                width: parent.width
                spacing: 8
                Text {
                    text: "Backlog"
                    color: "white"
                    font.pixelSize: 17
                    font.bold: true
                }
                Item { width: parent.width - 130; height: 1 }
                Button {
                    text: "Clear"
                    onClicked: Translator.clearBacklog()
                }
                Button {
                    text: "Close"
                    onClicked: Translator.backlogVisible = false
                }
            }

            ListView {
                width: parent.width
                height: parent.height - 46
                clip: true
                spacing: 8
                model: Translator.backlog
                delegate: Column {
                    width: ListView.view.width
                    spacing: 2
                    Text {
                        text: modelData.source
                        color: "#a8ffffff"
                        font.pixelSize: 12
                        width: parent.width
                        wrapMode: Text.Wrap
                    }
                    Text {
                        text: modelData.translation
                        color: "#fff4e6c8"
                        font.pixelSize: 16
                        width: parent.width
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }

    // Settings card, drawn inside the stream viewport.
    Rectangle {
        id: settingsPanel
        visible: root.settingsOpen && Chiaki.session !== null
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.72, 720)
        height: Math.min(settingsColumn.implicitHeight + 32, parent.height * 0.86)
        radius: 12
        color: "#f00e1014"
        border.color: "#55ffffff"
        border.width: 1

        ScrollView {
            anchors.fill: parent
            anchors.margins: 10
            clip: true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            Column {
                id: settingsColumn
                width: settingsPanel.width - 40
                spacing: 10

            Text {
                text: "Subtitle translator"
                color: "white"
                font.pixelSize: 22
                font.bold: true
            }

            RowLayout {
                width: parent.width
                spacing: 12
                Text { text: "Enabled"; color: "#dddddd"; font.pixelSize: 14 }
                Item { Layout.fillWidth: true }
                Switch {
                    checked: Translator.enabled
                    onToggled: {
                        Translator.enabled = checked
                        if (checked)
                            Translator.prepare()
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "OCR"; color: "#dddddd"; font.pixelSize: 14 }
                ComboBox {
                    id: ocrBox
                    Layout.fillWidth: true
                    model: ["PaddleOCR (PP-OCRv3 Japanese)", "RapidOCR (PP-OCRv5)", "Google Cloud Vision", "Gemini Vision (OCR + translate)", "OCR.space", "Chrome Screen AI (Linux)"]
                    currentIndex: Math.max(0, Translator.ocrProviderIds.indexOf(Translator.ocrProvider))
                    onActivated: Translator.ocrProvider = Translator.ocrProviderIds[index]
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                visible: Translator.ocrProvider === "google-vision" || Translator.ocrProvider === "gemini-vision" || Translator.ocrProvider === "ocrspace"
                Text { text: "OCR key"; color: "#dddddd"; font.pixelSize: 14 }
                TextField {
                    id: ocrKeyField
                    Layout.fillWidth: true
                    placeholderText: Translator.ocrProvider === "ocrspace" ? "helloworld (free)" : "Google / Gemini API key"
                    echoMode: TextInput.Password
                    text: Translator.ocrApiKey
                }
                Button {
                    text: "Save"
                    onClicked: {
                        Translator.setOcrApiKey(ocrKeyField.text)
                        Translator.applySettings()
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Backend"; color: "#dddddd"; font.pixelSize: 14 }
                ComboBox {
                    id: backendBox
                    Layout.fillWidth: true
                    model: ["Gemini Flash", "Google Translate (free)", "Google Cloud Translation",
                            "DeepL", "OpenCode Go", "OpenAI-compatible", "Offline NLLB"]
                    currentIndex: Math.max(0, Translator.backendIds.indexOf(Translator.backendId))
                    onActivated: Translator.backendId = Translator.backendIds[index]
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                visible: Translator.backendId !== "offline" && Translator.backendId !== "google"
                Text { text: "API key"; color: "#dddddd"; font.pixelSize: 14 }
                TextField {
                    id: apiKeyField
                    Layout.fillWidth: true
                    placeholderText: "stored in QSettings"
                    echoMode: TextInput.Password
                    text: Translator.apiKey(Translator.backendId)
                }
                Button {
                    text: "Save"
                    onClicked: {
                        Translator.setApiKey(Translator.backendId, apiKeyField.text)
                        Translator.applySettings()
                    }
                }
            }

            Text {
                width: parent.width
                visible: Translator.backendId === "google"
                text: "Google Translate (free, no API key). Google Cloud needs a key and the Cloud Translation API enabled."
                color: "#bbbbbb"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            RowLayout {
                width: parent.width
                spacing: 8
                visible: Translator.backendId === "opencode-go" || Translator.backendId === "openai"
                Text { text: "Base URL"; color: "#dddddd"; font.pixelSize: 14 }
                TextField {
                    id: baseUrlField
                    Layout.fillWidth: true
                    placeholderText: Translator.backendId === "opencode-go"
                        ? "https://opencode.ai/inference/go/openai/v1 (default)"
                        : "https://host/v1"
                    text: Translator.baseUrl(Translator.backendId)
                }
                Button {
                    text: "Save"
                    onClicked: {
                        Translator.setBaseUrl(Translator.backendId, baseUrlField.text)
                        Translator.applySettings()
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                visible: Translator.backendId === "opencode-go" || Translator.backendId === "openai"
                Text { text: "Model"; color: "#dddddd"; font.pixelSize: 14 }
                TextField {
                    id: modelField
                    Layout.fillWidth: true
                    placeholderText: "flash-class chat model"
                    text: Translator.model(Translator.backendId)
                }
                Button {
                    text: "Save"
                    onClicked: {
                        Translator.setModel(Translator.backendId, modelField.text)
                        Translator.applySettings()
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                visible: Translator.backendId === "offline"
                Text { text: "NLLB model dir"; color: "#dddddd"; font.pixelSize: 14 }
                TextField {
                    id: nllbField
                    Layout.fillWidth: true
                    placeholderText: "/path/to/nllb-200-distilled-600M-ct2-int8"
                    text: Translator.nllbModelPath
                }
                Button {
                    text: "Save"
                    onClicked: {
                        Translator.setNllbModelPath(nllbField.text)
                        Translator.applySettings()
                    }
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Target"; color: "#dddddd"; font.pixelSize: 14 }
                ComboBox {
                    model: ["Thai (th)", "English (en)"]
                    currentIndex: Translator.targetLanguage === "en" ? 1 : 0
                    onActivated: Translator.targetLanguage = index === 1 ? "en" : "th"
                }
                Text { text: "Preprocess"; color: "#dddddd"; font.pixelSize: 14 }
                ComboBox {
                    model: ["contrast", "threshold", "none"]
                    currentIndex: Math.max(0, ["contrast", "threshold", "none"].indexOf(Translator.preprocess))
                    onActivated: Translator.setPreprocess(["contrast", "threshold", "none"][index])
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Overlay"; color: "#dddddd"; font.pixelSize: 14 }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["In-place (over source text)", "Bottom center"]
                    currentIndex: Translator.overlayPlacement === "bottom" ? 1 : 0
                    onActivated: Translator.overlayPlacement = index === 1 ? "bottom" : "inplace"
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Hide after (s)"; color: "#dddddd"; font.pixelSize: 13 }
                SpinBox {
                    from: 0
                    to: 60
                    value: Translator.overlayAutoHideSeconds
                    onValueModified: Translator.overlayAutoHideSeconds = value
                }
                Text { text: "Scan interval (ms)"; color: "#dddddd"; font.pixelSize: 13 }
                SpinBox {
                    from: 50
                    to: 2000
                    stepSize: 50
                    value: Translator.scanIntervalMs
                    onValueModified: Translator.scanIntervalMs = value
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Stable time (ms)"; color: "#dddddd"; font.pixelSize: 13 }
                SpinBox {
                    from: 100
                    to: 5000
                    stepSize: 50
                    value: Translator.stableMs
                    onValueModified: Translator.stableMs = value
                }
                Text { text: "0 = overlay never auto-hides"; color: "#99ffffff"; font.pixelSize: 11 }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Overlay font"; color: "#dddddd"; font.pixelSize: 13 }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["System default"].concat(Qt.fontFamilies())
                    currentIndex: Translator.overlayFontFamily.length > 0
                        ? Math.max(0, model.indexOf(Translator.overlayFontFamily)) : 0
                    onActivated: Translator.overlayFontFamily = index === 0 ? "" : model[index]
                }
                Text { text: "Size"; color: "#dddddd"; font.pixelSize: 13 }
                SpinBox {
                    from: 0
                    to: 72
                    value: Translator.overlayFontSize
                    onValueModified: Translator.overlayFontSize = value
                }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "ROI preset"; color: "#dddddd"; font.pixelSize: 14 }
                TextField {
                    id: presetField
                    Layout.fillWidth: true
                    text: Translator.presetName
                    placeholderText: "preset name"
                }
                Button {
                    text: "Save"
                    onClicked: Translator.savePreset(presetField.text)
                }
                Button {
                    text: "Select ROI"
                    onClicked: {
                        root.settingsOpen = false
                        Translator.roiSelectionActive = true
                    }
                }
            }

            Text {
                width: parent.width
                text: "L4 translate now · R4 show/hide subtitles · L5 backlog · R5 select ROI\n" +
                      "Tap a source line to look a word up in JMdict. Offline NLLB requires a separate build."
                color: "#99ffffff"
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                text: "Hotkeys — combinations such as F13 or Ctrl+Shift+T; leave empty to disable"
                color: "#dddddd"
                font.pixelSize: 13
                font.bold: true
                wrapMode: Text.Wrap
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Live"; color: "#dddddd"; font.pixelSize: 13 }
                TextField {
                    id: shortcutEnableField
                    Layout.fillWidth: true
                    placeholderText: "F11"
                    text: Translator.shortcutEnable
                }
                Button { text: "Set"; onClicked: { root.captureAction = "enable"; shortcutCapture.forceActiveFocus() } }
                Text { text: "Translate"; color: "#dddddd"; font.pixelSize: 13 }
                TextField {
                    id: shortcutTranslateField
                    Layout.fillWidth: true
                    placeholderText: "F13"
                    text: Translator.shortcutTranslate
                }
                Button { text: "Set"; onClicked: { root.captureAction = "translate"; shortcutCapture.forceActiveFocus() } }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Overlay"; color: "#dddddd"; font.pixelSize: 13 }
                TextField {
                    id: shortcutOverlayField
                    Layout.fillWidth: true
                    placeholderText: "F14"
                    text: Translator.shortcutOverlay
                }
                Button { text: "Set"; onClicked: { root.captureAction = "overlay"; shortcutCapture.forceActiveFocus() } }
                Text { text: "Backlog"; color: "#dddddd"; font.pixelSize: 13 }
                TextField {
                    id: shortcutBacklogField
                    Layout.fillWidth: true
                    placeholderText: "F15"
                    text: Translator.shortcutBacklog
                }
                Button { text: "Set"; onClicked: { root.captureAction = "backlog"; shortcutCapture.forceActiveFocus() } }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Text { text: "Settings"; color: "#dddddd"; font.pixelSize: 13 }
                TextField {
                    id: shortcutSettingsField
                    Layout.fillWidth: true
                    placeholderText: "F12"
                    text: Translator.shortcutSettings
                }
                Button { text: "Set"; onClicked: { root.captureAction = "settings"; shortcutCapture.forceActiveFocus() } }
                Item { Layout.fillWidth: true }
            }

            RowLayout {
                width: parent.width
                spacing: 8
                Button {
                    text: "Save hotkeys"
                    onClicked: root.saveHotkeys()
                }
                Button {
                    text: "Reset hotkeys"
                    onClicked: {
                        Translator.resetShortcuts()
                        shortcutEnableField.text = Translator.shortcutEnable
                        shortcutTranslateField.text = Translator.shortcutTranslate
                        shortcutOverlayField.text = Translator.shortcutOverlay
                        shortcutBacklogField.text = Translator.shortcutBacklog
                        shortcutSettingsField.text = Translator.shortcutSettings
                    }
                }
                Item { Layout.fillWidth: true }
            }

            Text {
                width: parent.width
                text: Translator.statusText
                color: "#ffd27a"
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Item { height: 1; width: 1 }

            RowLayout {
                width: parent.width
                spacing: 8
                Button {
                    text: "Translate now"
                    onClicked: Translator.manualTranslate()
                }
                Button {
                    text: Translator.overlayVisible ? "Hide subtitles" : "Show subtitles"
                    onClicked: Translator.toggleOverlay()
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: "Close"
                    onClicked: root.settingsOpen = false
                }
            }
            }
        }
    }


    // Click "Set" then press a key combination instead of typing it.
    Item {
        id: shortcutCapture
        anchors.fill: parent
        z: 20000
        visible: root.captureAction.length > 0
        focus: visible

        Rectangle {
            anchors.fill: parent
            color: "#aa000000"
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.7, 560)
            height: 150
            radius: 12
            color: "#f00e1014"
            border.color: "#88ffffff"

            Column {
                anchors.centerIn: parent
                spacing: 12
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Press the new shortcut"
                    color: "white"
                    font.pixelSize: 22
                    font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Use modifiers such as Ctrl+Shift+T · Esc cancels · Backspace clears"
                    color: "#bbbbbb"
                    font.pixelSize: 12
                }
            }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {}
        }

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape) {
                root.captureAction = ""
                event.accepted = true
                return
            }
            if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                root.clearCapturedShortcut()
                event.accepted = true
                return
            }
            const sequence = Translator.shortcutFromKey(event.key, event.modifiers)
            if (sequence.length > 0) {
                root.assignCapturedShortcut(sequence)
                event.accepted = true
            }
        }

        onVisibleChanged: {
            if (visible)
                forceActiveFocus()
        }
    }

    // ROI selection layer: drag a rectangle, then it is saved to the preset.
    Item {
        id: roiLayer
        anchors.fill: parent
        visible: Translator.roiSelectionActive
        z: 5000

        property real startX: 0
        property real startY: 0
        property real selectionX: 0
        property real selectionY: 0
        property real selectionW: 0
        property real selectionH: 0
        property bool selecting: false

        onVisibleChanged: {
            if (visible) {
                const roi = Translator.roiNormalized
                selectionX = roi.x * width
                selectionY = roi.y * height
                selectionW = roi.width * width
                selectionH = roi.height * height
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "#66000000"
        }

        Rectangle {
            id: selectionRect
            x: roiLayer.selectionX
            y: roiLayer.selectionY
            width: roiLayer.selectionW
            height: roiLayer.selectionH
            color: "#2200a7ff"
            border.color: "#00a7ff"
            border.width: 2
        }

        Text {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 30
            text: "Drag to select the subtitle area · release to save"
            color: "white"
            font.pixelSize: 16
            style: Text.Outline
            styleColor: "#99000000"
        }

        MouseArea {
            anchors.fill: parent
            onPressed: function(mouse) {
                roiLayer.startX = mouse.x
                roiLayer.startY = mouse.y
                roiLayer.selectionX = mouse.x
                roiLayer.selectionY = mouse.y
                roiLayer.selectionW = 0
                roiLayer.selectionH = 0
                roiLayer.selecting = true
            }
            onPositionChanged: function(mouse) {
                if (!roiLayer.selecting)
                    return
                roiLayer.selectionX = Math.min(roiLayer.startX, mouse.x)
                roiLayer.selectionY = Math.min(roiLayer.startY, mouse.y)
                roiLayer.selectionW = Math.abs(mouse.x - roiLayer.startX)
                roiLayer.selectionH = Math.abs(mouse.y - roiLayer.startY)
            }
            onReleased: function(mouse) {
                roiLayer.selecting = false
                if (roiLayer.selectionW < 24 || roiLayer.selectionH < 12) {
                    Translator.roiSelectionActive = false
                    return
                }
                Translator.setRoiNormalized(roiLayer.selectionX / width,
                                            roiLayer.selectionY / height,
                                            roiLayer.selectionW / width,
                                            roiLayer.selectionH / height)
                Translator.roiSelectionActive = false
            }
        }
    }
}
