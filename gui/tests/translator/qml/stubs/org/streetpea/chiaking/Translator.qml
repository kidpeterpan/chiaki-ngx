// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Stand-in for TranslatorManager in the QML tests.
pragma Singleton
import QtQuick

QtObject {
    property bool enabled: false
    property bool prepared: false
    property bool roiSelectionActive: false
    property bool backlogVisible: false
    property bool overlayVisible: true
    property string statusText: ""
    property string backendId: "google"
    readonly property var backendIds: ["gemini", "google", "google-cloud", "deepl", "opencode-go", "openai", "offline"]
    property string ocrProvider: "paddle"
    readonly property var ocrProviderIds: ["paddle", "rapidocr", "google-vision", "gemini-vision", "ocrspace", "chromescreenai"]
    property string ocrApiKey: ""
    property string preprocess: "contrast"
    property int scanIntervalMs: 100
    property int stableMs: 250
    property int overlayAutoHideSeconds: 6
    property string overlayPlacement: "inplace"
    property string overlayFontFamily: ""
    property int overlayFontSize: 0
    property string targetLanguage: "th"
    property string presetName: "default"
    property string nllbModelPath: ""
    property string shortcutEnable: "F11"
    property string shortcutTranslate: "F13"
    property string shortcutOverlay: "F14"
    property string shortcutBacklog: "F15"
    property string shortcutSettings: "F12"
    property string overlayText: ""
    property var overlayRegions: []
    property var dictionaryEntries: []
    property string dictionaryWord: ""
    property var backlog: []
    property string sourceText: ""
    property var roiNormalized: ({ x: 0.1, y: 0.7, width: 0.8, height: 0.2 })
    property var keys: ({})
    property int prepareCount: 0
    property int applyCount: 0
    signal settingsRequested()

    function apiKey(id) { return keys[id] || "" }
    function setApiKey(id, key) { const copy = Object.assign({}, keys); copy[id] = key; keys = copy }
    function setOcrApiKey(key) { ocrApiKey = key }
    function setPreprocess(mode) { preprocess = mode }
    function baseUrl(id) { return "" }
    function model(id) { return "" }
    function setBaseUrl(id, url) {}
    function setModel(id, name) {}
    function setNllbModelPath(path) { nllbModelPath = path }
    function savePreset(name) { presetName = name }
    function prepare() { prepareCount++ }
    function applySettings() { applyCount++ }
    function manualTranslate() {}
    function toggleOverlay() { overlayVisible = !overlayVisible }
    function toggleBacklog() { backlogVisible = !backlogVisible }
    function toggleEnabled() { enabled = !enabled }
    function clearBacklog() {}
    function resetShortcuts() {}
    function setShortcuts(a, b, c, d, e) { return true }
    function shortcutFromKey(key, modifiers) { return "" }
    function lookupAt(x, y) {}
    function lookupWord(word) {}
    function setRoiNormalized(x, y, w, h) {}
    function videoViewport(a, b, c, d) { return Qt.rect(0, 0, 1, 1) }
}
