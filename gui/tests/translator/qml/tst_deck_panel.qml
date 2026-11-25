// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Drives the translator settings panel the way a Steam Deck controller does: QmlController
// sends the D-pad as arrow keys, A as Return, B as Escape and L1/R1 as PageUp/PageDown.
import QtQuick
import QtTest
import org.streetpea.chiaking
import "../../../src/qml/translator"

Item {
    id: host
    width: 1280
    height: 800

    TranslatorOverlay { id: overlay }

    TestCase {
        name: "DeckPanel"
        when: windowShown

        function focused() { return host.Window.activeFocusItem }
        function focusedText() {
            const item = focused()
            return item && item.text !== undefined ? String(item.text) : "<" + item + ">"
        }

        function init() {
            // A list left open by a failed case would keep focus and fail every later case.
            const item = focused()
            if (item && item.popup !== undefined && item.popup.visible) {
                item.popup.close()
                wait(300)
            }
            overlay.settingsOpen = false
            Translator.enabled = false
            Translator.liveTranslation = true
            Translator.overlayShowSource = true
            Translator.overlayBoxOpacity = 80
            Translator.overlayRegions = []
            Translator.backendId = "google"
            Translator.ocrProvider = "paddle"
            Translator.keys = ({})
            Translator.preprocess = "contrast"
            Translator.scanIntervalMs = 100
            Translator.applyCount = 0
            Translator.prepareCount = 0
            wait(20)
        }

        function openPanel() {
            overlay.settingsOpen = true
            wait(60)
        }

        function test_opens_with_focus_on_first_control() {
            openPanel()
            compare(focusedText(), "Turn on")
        }

        function test_dpad_moves_and_a_presses() {
            openPanel()
            keyClick(Qt.Key_Right)
            compare(focusedText(), "Change")
            keyClick(Qt.Key_Left)
            compare(focusedText(), "Turn on")
            keyClick(Qt.Key_Down)
            compare(focusedText(), "Setup")
            keyClick(Qt.Key_Down)
            compare(focused().deckNavigable, true)
            verify(focused().model !== undefined, "Down from a tab enters the section")
            keyClick(Qt.Key_Up)
            compare(focusedText(), "Setup")
            keyClick(Qt.Key_Up)
            compare(focusedText(), "Turn on")
            keyClick(Qt.Key_Return)
            verify(Translator.enabled)
            compare(Translator.prepareCount, 1)
        }

        function test_r1_switches_section_and_arrows_change_values() {
            openPanel()
            keyClick(Qt.Key_PageDown)
            compare(focusedText(), "Tuning")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(Translator.preprocess, "threshold")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Right)
            compare(Translator.scanIntervalMs, 150)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Left)
            compare(Translator.scanIntervalMs, 50)
        }

        function test_b_closes_and_applies_once() {
            Translator.enabled = true
            openPanel()
            keyClick(Qt.Key_Escape)
            verify(!overlay.settingsOpen)
            compare(Translator.applyCount, 1)
        }

        function test_typed_key_is_saved_when_panel_closes() {
            Translator.backendId = "gemini"
            Translator.enabled = true
            openPanel()
            compare(overlay.engineIssue, "api-key")
            compare(focusedText(), "Add key")
            keyClick(Qt.Key_Return)
            wait(60)
            compare(focused().echoMode, TextInput.Password)
            keyClick("a")
            keyClick("b")
            keyClick("c")
            keyClick(Qt.Key_Escape)
            verify(!overlay.settingsOpen)
            compare(Translator.apiKey("gemini"), "abc")
            compare(overlay.engineIssue, "")
            compare(Translator.applyCount, 1)
        }

        // QmlController sends A as a press immediately followed by a release; the list must
        // stay open after both, and D-pad down then A must choose the next option.
        function test_a_opens_dropdown_list_and_chooses_an_option() {
            openPanel()
            keyClick(Qt.Key_PageDown)
            keyClick(Qt.Key_Down)
            const combo = focused()
            verify(combo.popup !== undefined, "focus is on the Image cleanup dropdown")
            keyClick(Qt.Key_Return)
            wait(400)
            verify(combo.popup.visible, "A opens the list and it stays open")
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Return)
            wait(400)
            verify(!combo.popup.visible, "A on an option closes the list")
            compare(Translator.preprocess, "threshold")
        }

        // OFF with Live switched off: tapping the badge must turn on both, not just the translator.
        function test_live_badge_turns_translator_on() {
            Translator.liveTranslation = false
            const badge = findChild(overlay, "liveBadge")
            verify(badge, "live badge exists")
            compare(findChild(badge, "liveBadgeText").text, "OFF")
            mouseClick(badge)
            verify(Translator.enabled)
            verify(Translator.liveTranslation)
            compare(findChild(badge, "liveBadgeText").text, "LIVE")
        }

        function test_live_off_shows_manual_badge() {
            Translator.enabled = true
            Translator.liveTranslation = false
            const badge = findChild(overlay, "liveBadge")
            compare(findChild(badge, "liveBadgeText").text, "MANUAL")
            mouseClick(badge)
            verify(Translator.liveTranslation)
            compare(findChild(badge, "liveBadgeText").text, "LIVE")
        }

        function test_setup_live_row_switches_live() {
            Translator.enabled = true
            openPanel()
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            const combo = focused()
            verify(combo.model !== undefined, "Down from the Setup tab reaches the first Setup row")
            compare(combo.currentText, "On")
            keyClick(Qt.Key_Right)
            verify(!Translator.liveTranslation)
            verify(Translator.enabled, "Live off keeps the translator on")
        }

        // A dialogue box about as tall as a game's (two short lines), with a translation that
        // auto-fit has to shrink while the original line takes its share of the box.
        function test_hidden_source_gives_translation_the_box() {
            Translator.enabled = true
            Translator.overlayRegions = [{ x: 0.1, y: 0.8, width: 0.5, height: 0.07,
                                           source: "今日はいい天気ですね。散歩に行きましょう。",
                                           translation: "วันนี้อากาศดีนะ ไปเดินเล่นกันเถอะ แล้วค่อยกลับมากินข้าวด้วยกัน" }]
            wait(20)
            const block = findChild(overlay, "regionBlock")
            const source = findChild(block, "regionSource")
            const translation = findChild(block, "regionTranslation")
            verify(source.visible)
            verify(translation.height < block.height - 16)
            const fittedSize = translation.fontInfo.pixelSize
            Translator.overlayShowSource = false
            wait(20)
            verify(!source.visible)
            compare(translation.height, block.height - 16)
            verify(translation.fontInfo.pixelSize > fittedSize, "auto-fit translation grows into the freed space")
        }

        function test_box_opacity_sets_background_alpha() {
            Translator.overlayRegions = [{ x: 0.1, y: 0.6, width: 0.8, height: 0.25,
                                           source: "こんにちは", translation: "สวัสดี" }]
            wait(20)
            fuzzyCompare(findChild(overlay, "subtitleBox").color.a, 0.8, 0.01)
            Translator.overlayBoxOpacity = 40
            wait(20)
            fuzzyCompare(findChild(overlay, "subtitleBox").color.a, 0.4, 0.01)
            fuzzyCompare(findChild(overlay, "regionBlock").color.a, 0.4, 0.01)
        }

        function test_use_free_google_clears_missing_key() {
            Translator.backendId = "deepl"
            openPanel()
            compare(overlay.engineIssue, "api-key")
            keyClick(Qt.Key_Right)
            compare(focusedText(), "Use free Google")
            keyClick(Qt.Key_Return)
            compare(Translator.backendId, "google")
            compare(overlay.engineIssue, "")
        }

        // QmlController keeps sending D-pad, A and L1/R1 as keys during gameplay. Once the
        // panel is closed none of them may reach a hidden panel control.
        function test_keys_after_close_do_not_reach_hidden_controls() {
            Translator.enabled = true
            openPanel()
            keyClick(Qt.Key_PageDown)
            keyClick(Qt.Key_Down)
            keyClick(Qt.Key_Down)
            compare(focused().value, 100)
            keyClick(Qt.Key_Escape)
            verify(!overlay.settingsOpen)
            const sectionBefore = overlay.settingsSection
            keyClick(Qt.Key_Right)
            keyClick(Qt.Key_Left)
            keyClick(Qt.Key_Return)
            keyClick(Qt.Key_PageDown)
            compare(Translator.scanIntervalMs, 100)
            verify(Translator.enabled)
            compare(overlay.settingsSection, sectionBefore)
            const item = focused()
            verify(!item || item.deckNavigable !== true, "a Deck control kept focus after close")
        }
    }
}
