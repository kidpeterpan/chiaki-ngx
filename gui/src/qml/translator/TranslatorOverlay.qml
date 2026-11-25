// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// In-viewport translator UI. Deliberately a child of the stream view rather
// than a separate window so Gamescope keeps the game focused.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import org.streetpea.chiaking

import "DeckNav.js" as DeckNav

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

    TranslatorTheme { id: theme }

    // Translator.apiKey() has no change signal; bumped after a key is saved so the
    // setup checklist re-reads it.
    property int keyRevision: 0
    property Item focusBeforeSettings: null
    readonly property var backendNames: ["Gemini Flash", "Google Translate (free)", "Google Cloud Translation",
        "DeepL", "OpenCode Go", "OpenAI-compatible", "Offline NLLB"]
    // "" when translation can run; otherwise what is missing: "ocr-key", "api-key" or "nllb".
    readonly property string engineIssue: computeEngineIssue()

    function computeEngineIssue() {
        if (keyRevision < 0)
            return ""
        const ocr = Translator.ocrProvider
        if ((ocr === "google-vision" || ocr === "gemini-vision") && Translator.ocrApiKey.length === 0)
            return "ocr-key"
        if (ocr === "gemini-vision")
            return ""
        const backend = Translator.backendId
        if (backend === "google" || backend === "openai")
            return ""
        if (backend === "offline")
            return Translator.nllbModelPath.length > 0 ? "" : "nllb"
        return Translator.apiKey(backend).length > 0 ? "" : "api-key"
    }

    function backendName(backendId) {
        const index = Translator.backendIds.indexOf(backendId)
        return index >= 0 ? backendNames[index] : backendId
    }

    function engineStateText() {
        if (engineIssue === "ocr-key")
            return "Needs an OCR API key"
        if (engineIssue === "api-key")
            return "Needs a " + backendName(Translator.backendId) + " API key"
        if (engineIssue === "nllb")
            return "Needs the NLLB model folder"
        return Translator.ocrProvider === "gemini-vision" ? "Gemini Vision" : backendName(Translator.backendId)
    }

    // "error", "ok" or "info", so the status line pairs its colour with an icon.
    function statusKind(text) {
        if (/^error|failed|missing|not found|requires|^add a|^set a|unsupported|not available/i.test(text))
            return "error"
        if (/ready|^ocr \d/i.test(text))
            return "ok"
        return "info"
    }

    function setTranslatorEnabled(enabled) {
        Translator.enabled = enabled
        if (enabled)
            Translator.prepare()
    }

    function startRoiSelection() {
        root.settingsOpen = false
        Translator.roiSelectionActive = true
    }
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

    onSettingsOpenChanged: {
        if (settingsOpen) {
            focusBeforeSettings = root.Window.activeFocusItem
            Qt.callLater(settingsPanel.focusFirst)
            return
        }
        // Text fields save on editingFinished; a field still being edited has not fired
        // it yet, so fire it now to store the value before the settings are applied.
        const focused = root.Window.activeFocusItem
        if (focused && focused.deckNavigable === true && focused.editingFinished !== undefined)
            focused.editingFinished()
        if (Translator.enabled && Chiaki.session)
            Translator.applySettings()
        // Hand focus back so the stream view's own keys keep working.
        if (focusBeforeSettings && focusBeforeSettings.visible)
            focusBeforeSettings.forceActiveFocus()
        focusBeforeSettings = null
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
        width: theme.launcherSize
        height: theme.launcherSize
        radius: theme.launcherSize / 2
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        color: settingsOpen ? theme.accent : theme.launcher
        border.color: "#66ffffff"
        border.width: 1
        visible: Chiaki.session !== null

        Text {
            anchors.centerIn: parent
            text: "訳"
            color: theme.textStrong
            font.pixelSize: 22
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

    // Settings card, drawn inside the stream viewport. The header (title, setup
    // checklist, section tabs) and the footer (status, actions) stay in place; only the
    // selected section scrolls. Every control is a Deck* control, so the D-pad moves
    // between them, left/right changes values, A presses, B closes, L1/R1 switch sections.
    Rectangle {
        id: settingsPanel

        readonly property bool deckNavScope: true
        readonly property var sectionNames: ["Setup", "Tuning", "Appearance", "Advanced"]
        property int section: 0

        function focusControl(item) {
            item.forceActiveFocus(Qt.TabFocusReason)
            ensureVisible(item)
        }

        function ensureVisible(item) {
            let inside = false
            for (let node = item; node; node = node.parent) {
                if (node === sectionColumn) {
                    inside = true
                    break
                }
            }
            if (!inside)
                return
            const top = item.mapToItem(sectionColumn, 0, 0).y - theme.gap
            const bottom = top + item.height + 2 * theme.gap
            const maxY = Math.max(0, sectionFlick.contentHeight - sectionFlick.height)
            if (top < sectionFlick.contentY)
                sectionFlick.contentY = Math.max(0, top)
            else if (bottom > sectionFlick.contentY + sectionFlick.height)
                sectionFlick.contentY = Math.min(maxY, bottom - sectionFlick.height)
        }

        // Opens on the action that unblocks translation, if anything is missing.
        function focusFirst() {
            if (!visible)
                return
            if (engineFixButton.visible) {
                focusControl(engineFixButton)
                return
            }
            const items = DeckNav.navigableItems(settingsPanel)
            if (items.length > 0)
                focusControl(items[0])
        }

        function cycleSection(step) {
            section = (section + step + sectionNames.length) % sectionNames.length
            sectionFlick.contentY = 0
            const tab = sectionTabs.itemAt(section)
            if (tab)
                focusControl(tab)
        }

        function showSetupField(field) {
            section = 0
            Qt.callLater(function() {
                if (field.visible)
                    focusControl(field)
            })
        }

        visible: root.settingsOpen && Chiaki.session !== null
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 820)
        height: Math.min(parent.height * 0.92, panelLayout.implicitHeight + 2 * panelLayout.anchors.margins)
        radius: theme.radius
        color: theme.panel
        border.color: theme.panelBorder
        border.width: 1

        Keys.onPressed: function(event) {
            if (event.key === Qt.Key_Escape)
                root.settingsOpen = false
            else if (event.key === Qt.Key_PageUp)
                cycleSection(-1)
            else if (event.key === Qt.Key_PageDown)
                cycleSection(1)
            else
                return
            event.accepted = true
        }

        ColumnLayout {
            id: panelLayout
            anchors.fill: parent
            anchors.margins: theme.gap + 6
            spacing: theme.gap

            Text {
                text: "Subtitle translator"
                color: theme.textStrong
                font.pixelSize: theme.titleSize
                font.bold: true
            }

            // Setup checklist: what translation needs, with the button that fixes each.
            RowLayout {
                Layout.fillWidth: true
                spacing: theme.gap

                TranslatorChecklistItem {
                    title: "Translator"
                    ok: Translator.enabled
                    stateText: Translator.enabled ? "On" : "Off"
                    DeckButton {
                        text: Translator.enabled ? "Turn off" : "Turn on"
                        onClicked: root.setTranslatorEnabled(!Translator.enabled)
                    }
                }

                TranslatorChecklistItem {
                    title: "Translation engine"
                    ok: root.engineIssue.length === 0
                    stateText: root.engineStateText()
                    DeckButton {
                        id: engineFixButton
                        visible: root.engineIssue.length > 0
                        text: root.engineIssue === "nllb" ? "Set folder" : "Add key"
                        onClicked: settingsPanel.showSetupField(root.engineIssue === "ocr-key" ? ocrKeyField
                            : root.engineIssue === "nllb" ? nllbField : apiKeyField)
                    }
                    DeckButton {
                        visible: root.engineIssue === "api-key"
                        text: "Use free Google"
                        onClicked: Translator.backendId = "google"
                    }
                    DeckButton {
                        visible: root.engineIssue.length === 0
                        text: "Change"
                        onClicked: settingsPanel.showSetupField(backendBox)
                    }
                }

                TranslatorChecklistItem {
                    title: "Subtitle area"
                    stateText: "Preset: " + (Translator.presetName.length > 0 ? Translator.presetName : "default")
                    DeckButton {
                        text: "Select area"
                        onClicked: root.startRoiSelection()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    id: sectionTabs
                    model: settingsPanel.sectionNames

                    DeckButton {
                        required property int index
                        required property string modelData
                        // Moving up into the tab row lands on the selected tab.
                        readonly property bool deckPreferred: settingsPanel.section === index
                        text: modelData
                        highlighted: settingsPanel.section === index
                        flat: settingsPanel.section !== index
                        onClicked: {
                            settingsPanel.section = index
                            sectionFlick.contentY = 0
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.divider
            }

            Flickable {
                id: sectionFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: sectionColumn.implicitHeight
                Layout.minimumHeight: Math.min(sectionColumn.implicitHeight, 3 * theme.rowHeight)
                contentWidth: width
                contentHeight: sectionColumn.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: sectionColumn
                    width: sectionFlick.width - 14
                    spacing: 6

                    // Setup: what to read, how to translate it, and where the subtitles are.
                    Column {
                        width: parent.width
                        spacing: 6
                        visible: settingsPanel.section === 0

                        TranslatorRow {
                            label: "OCR"
                            DeckComboBox {
                                id: ocrBox
                                Layout.fillWidth: true
                                model: ["PaddleOCR (PP-OCRv3 Japanese)", "RapidOCR (PP-OCRv5)", "Google Cloud Vision", "Gemini Vision (OCR + translate)", "OCR.space", "Chrome Screen AI (Linux)"]
                                currentIndex: Math.max(0, Translator.ocrProviderIds.indexOf(Translator.ocrProvider))
                                onActivated: index => Translator.ocrProvider = Translator.ocrProviderIds[index]
                            }
                        }

                        TranslatorRow {
                            label: "OCR key"
                            visible: Translator.ocrProvider === "google-vision" || Translator.ocrProvider === "gemini-vision" || Translator.ocrProvider === "ocrspace"
                            DeckTextField {
                                id: ocrKeyField
                                Layout.fillWidth: true
                                placeholderText: Translator.ocrProvider === "ocrspace" ? "helloworld (free)" : "Google / Gemini API key"
                                echoMode: TextInput.Password
                                text: Translator.ocrApiKey
                                onEditingFinished: Translator.setOcrApiKey(text)
                            }
                        }

                        TranslatorRow {
                            label: "Translation"
                            DeckComboBox {
                                id: backendBox
                                Layout.fillWidth: true
                                model: root.backendNames
                                currentIndex: Math.max(0, Translator.backendIds.indexOf(Translator.backendId))
                                onActivated: index => Translator.backendId = Translator.backendIds[index]
                            }
                        }

                        TranslatorRow {
                            label: "API key"
                            visible: Translator.backendId !== "offline" && Translator.backendId !== "google"
                            hint: "Saved when you leave the field."
                            DeckTextField {
                                id: apiKeyField
                                Layout.fillWidth: true
                                placeholderText: "stored on this device"
                                echoMode: TextInput.Password
                                text: Translator.apiKey(Translator.backendId)
                                onEditingFinished: {
                                    Translator.setApiKey(Translator.backendId, text)
                                    root.keyRevision++
                                }
                            }
                        }

                        Text {
                            width: parent.width
                            visible: Translator.backendId === "google"
                            text: "Google Translate (free) needs no key. Gemini or DeepL usually translate better and need a key."
                            color: theme.textMuted
                            font.pixelSize: theme.captionSize
                            wrapMode: Text.Wrap
                        }

                        TranslatorRow {
                            label: "Base URL"
                            visible: Translator.backendId === "opencode-go" || Translator.backendId === "openai"
                            DeckTextField {
                                Layout.fillWidth: true
                                placeholderText: Translator.backendId === "opencode-go"
                                    ? "https://opencode.ai/inference/go/openai/v1 (default)"
                                    : "https://host/v1"
                                text: Translator.baseUrl(Translator.backendId)
                                onEditingFinished: Translator.setBaseUrl(Translator.backendId, text)
                            }
                        }

                        TranslatorRow {
                            label: "Model"
                            visible: Translator.backendId === "opencode-go" || Translator.backendId === "openai"
                            DeckTextField {
                                Layout.fillWidth: true
                                placeholderText: "flash-class chat model"
                                text: Translator.model(Translator.backendId)
                                onEditingFinished: Translator.setModel(Translator.backendId, text)
                            }
                        }

                        TranslatorRow {
                            label: "NLLB model folder"
                            visible: Translator.backendId === "offline"
                            hint: "Offline NLLB needs a build with CTranslate2."
                            DeckTextField {
                                id: nllbField
                                Layout.fillWidth: true
                                placeholderText: "/path/to/nllb-200-distilled-600M-ct2-int8"
                                text: Translator.nllbModelPath
                                onEditingFinished: Translator.setNllbModelPath(text)
                            }
                        }

                        TranslatorRow {
                            label: "Translate into"
                            DeckComboBox {
                                Layout.fillWidth: true
                                model: ["Thai", "English"]
                                currentIndex: Translator.targetLanguage === "en" ? 1 : 0
                                onActivated: index => Translator.targetLanguage = index === 1 ? "en" : "th"
                            }
                        }

                        TranslatorRow {
                            label: "Area preset"
                            hint: "Saves the current subtitle area under this name."
                            DeckTextField {
                                id: presetField
                                Layout.fillWidth: true
                                text: Translator.presetName
                                placeholderText: "preset name"
                            }
                            DeckButton {
                                text: "Save preset"
                                onClicked: Translator.savePreset(presetField.text)
                            }
                        }
                    }

                    // Tuning: when and how the subtitle area is read.
                    Column {
                        width: parent.width
                        spacing: 6
                        visible: settingsPanel.section === 1

                        TranslatorRow {
                            label: "Image cleanup"
                            hint: "Black & white helps when subtitles sit on a busy or see-through background."
                            DeckComboBox {
                                readonly property var modes: ["contrast", "threshold", "none"]
                                Layout.fillWidth: true
                                model: ["Contrast boost (default)", "Black & white", "None"]
                                currentIndex: Math.max(0, modes.indexOf(Translator.preprocess))
                                onActivated: index => Translator.setPreprocess(modes[index])
                            }
                        }

                        TranslatorRow {
                            label: "Scan interval (ms)"
                            hint: "How often the subtitle area is checked. Lower reacts faster and uses more CPU."
                            DeckSpinBox {
                                from: 50
                                to: 2000
                                stepSize: 50
                                value: Translator.scanIntervalMs
                                onValueModified: Translator.scanIntervalMs = value
                            }
                        }

                        TranslatorRow {
                            label: "Stable time (ms)"
                            hint: "How long text must stay still before it is translated."
                            DeckSpinBox {
                                from: 100
                                to: 5000
                                stepSize: 50
                                value: Translator.stableMs
                                onValueModified: Translator.stableMs = value
                            }
                        }
                    }

                    // Appearance: where and how translations are drawn.
                    Column {
                        width: parent.width
                        spacing: 6
                        visible: settingsPanel.section === 2

                        TranslatorRow {
                            label: "Placement"
                            DeckComboBox {
                                Layout.fillWidth: true
                                model: ["Over the original text", "Bottom center"]
                                currentIndex: Translator.overlayPlacement === "bottom" ? 1 : 0
                                onActivated: index => Translator.overlayPlacement = index === 1 ? "bottom" : "inplace"
                            }
                        }

                        TranslatorRow {
                            label: "Hide after (s)"
                            hint: "0 = never hide."
                            DeckSpinBox {
                                from: 0
                                to: 60
                                value: Translator.overlayAutoHideSeconds
                                onValueModified: Translator.overlayAutoHideSeconds = value
                            }
                        }

                        TranslatorRow {
                            label: "Font"
                            DeckComboBox {
                                Layout.fillWidth: true
                                model: ["System default"].concat(Qt.fontFamilies())
                                currentIndex: Translator.overlayFontFamily.length > 0
                                    ? Math.max(0, model.indexOf(Translator.overlayFontFamily)) : 0
                                onActivated: index => Translator.overlayFontFamily = index === 0 ? "" : model[index]
                            }
                        }

                        TranslatorRow {
                            label: "Font size"
                            hint: "0 = fit the subtitle box."
                            DeckSpinBox {
                                from: 0
                                to: 72
                                value: Translator.overlayFontSize
                                onValueModified: Translator.overlayFontSize = value
                            }
                        }
                    }

                    // Advanced: controller help and keyboard hotkeys for desktop use.
                    Column {
                        width: parent.width
                        spacing: 6
                        visible: settingsPanel.section === 3

                        Text {
                            width: parent.width
                            text: "Steam Deck: L4 translate now · R4 show/hide subtitles · L5 backlog · R5 select area.\n" +
                                  "Tap a source line to look a word up in JMdict."
                            color: theme.textMuted
                            font.pixelSize: theme.captionSize
                            wrapMode: Text.Wrap
                        }

                        Text {
                            width: parent.width
                            text: "Keyboard hotkeys (desktop). Use Set and press the keys, or type a combination such as Ctrl+Shift+T. Leave empty to disable."
                            color: theme.text
                            font.pixelSize: theme.captionSize
                            wrapMode: Text.Wrap
                        }

                        TranslatorRow {
                            label: "Live on/off"
                            DeckTextField {
                                id: shortcutEnableField
                                Layout.fillWidth: true
                                placeholderText: "F11"
                                text: Translator.shortcutEnable
                                onEditingFinished: root.saveHotkeys()
                            }
                            DeckButton {
                                text: "Set"
                                onClicked: { root.captureAction = "enable"; shortcutCapture.forceActiveFocus() }
                            }
                        }

                        TranslatorRow {
                            label: "Translate now"
                            DeckTextField {
                                id: shortcutTranslateField
                                Layout.fillWidth: true
                                placeholderText: "F13"
                                text: Translator.shortcutTranslate
                                onEditingFinished: root.saveHotkeys()
                            }
                            DeckButton {
                                text: "Set"
                                onClicked: { root.captureAction = "translate"; shortcutCapture.forceActiveFocus() }
                            }
                        }

                        TranslatorRow {
                            label: "Show/hide subtitles"
                            DeckTextField {
                                id: shortcutOverlayField
                                Layout.fillWidth: true
                                placeholderText: "F14"
                                text: Translator.shortcutOverlay
                                onEditingFinished: root.saveHotkeys()
                            }
                            DeckButton {
                                text: "Set"
                                onClicked: { root.captureAction = "overlay"; shortcutCapture.forceActiveFocus() }
                            }
                        }

                        TranslatorRow {
                            label: "Backlog"
                            DeckTextField {
                                id: shortcutBacklogField
                                Layout.fillWidth: true
                                placeholderText: "F15"
                                text: Translator.shortcutBacklog
                                onEditingFinished: root.saveHotkeys()
                            }
                            DeckButton {
                                text: "Set"
                                onClicked: { root.captureAction = "backlog"; shortcutCapture.forceActiveFocus() }
                            }
                        }

                        TranslatorRow {
                            label: "Open settings"
                            DeckTextField {
                                id: shortcutSettingsField
                                Layout.fillWidth: true
                                placeholderText: "F12"
                                text: Translator.shortcutSettings
                                onEditingFinished: root.saveHotkeys()
                            }
                            DeckButton {
                                text: "Set"
                                onClicked: { root.captureAction = "settings"; shortcutCapture.forceActiveFocus() }
                            }
                        }

                        DeckButton {
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
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: theme.divider
            }

            Text {
                readonly property string kind: root.statusKind(Translator.statusText)
                Layout.fillWidth: true
                visible: Translator.statusText.length > 0
                text: (kind === "error" ? "⚠ " : kind === "ok" ? "✓ " : "• ") + Translator.statusText +
                      (Translator.statusText === "No text in ROI" ? " — try Select area or Translate now" : "")
                color: kind === "error" ? theme.error : kind === "ok" ? theme.ok : theme.text
                font.pixelSize: theme.captionSize + 1
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.gap

                DeckButton {
                    text: "Translate now"
                    onClicked: Translator.manualTranslate()
                }
                DeckButton {
                    text: Translator.overlayVisible ? "Hide subtitles" : "Show subtitles"
                    onClicked: Translator.toggleOverlay()
                }
                Item { Layout.fillWidth: true }
                DeckButton {
                    text: "Close"
                    onClicked: root.settingsOpen = false
                }
            }

            Text {
                Layout.fillWidth: true
                text: "Changes apply when you close this panel · D-pad move · ←/→ change · A select · B close · L1/R1 section"
                color: theme.textMuted
                font.pixelSize: theme.captionSize
                wrapMode: Text.Wrap
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
