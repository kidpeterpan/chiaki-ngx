// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Translator combo box for a controller: up/down moves between controls, left/right
// steps through the options, A opens the list. Stepping emits activated(), so the
// owner's onActivated handler stores the choice exactly as a tap would.
import QtQuick
import QtQuick.Controls

import "DeckNav.js" as DeckNav

ComboBox {
    id: control

    readonly property bool deckNavigable: true
    property TranslatorTheme theme: TranslatorTheme {}

    function step(delta) {
        const next = currentIndex + delta
        if (next >= 0 && next < count)
            activated(next)
    }

    activeFocusOnTab: true
    font.pixelSize: theme.bodySize

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        // An open list handles its own keys: up/down highlight, A selects, B closes.
        if (popup.visible)
            return
        switch (event.key) {
        case Qt.Key_Up:
            DeckNav.moveVertical(control, false)
            break
        case Qt.Key_Down:
            DeckNav.moveVertical(control, true)
            break
        case Qt.Key_Left:
            step(-1)
            break
        case Qt.Key_Right:
            step(1)
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            popup.open()
            break
        case Qt.Key_PageUp:
            DeckNav.switchSection(control, -1)
            break
        case Qt.Key_PageDown:
            DeckNav.switchSection(control, 1)
            break
        default:
            return
        }
        event.accepted = true
    }

    DeckFocusRing {
        target: control
        theme: control.theme
    }
}
