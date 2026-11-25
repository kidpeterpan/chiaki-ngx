// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Translator text field for a controller: up/down moves between controls; left/right
// keep moving the cursor. Owners save in onEditingFinished (Return or leaving the field).
import QtQuick
import QtQuick.Controls

import "DeckNav.js" as DeckNav

TextField {
    id: control

    readonly property bool deckNavigable: true
    property TranslatorTheme theme: TranslatorTheme {}

    activeFocusOnTab: true
    font.pixelSize: theme.bodySize

    Keys.priority: Keys.BeforeItem
    Keys.onPressed: function(event) {
        switch (event.key) {
        case Qt.Key_Up:
            DeckNav.moveVertical(control, false)
            break
        case Qt.Key_Down:
            DeckNav.moveVertical(control, true)
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
