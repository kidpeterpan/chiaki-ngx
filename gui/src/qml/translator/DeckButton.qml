// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Translator button that a controller can reach: the D-pad moves between controls and
// A (Return) presses it.
import QtQuick
import QtQuick.Controls

import "DeckNav.js" as DeckNav

Button {
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
        case Qt.Key_Left:
            DeckNav.move(control, false)
            break
        case Qt.Key_Right:
            DeckNav.move(control, true)
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
            control.clicked()
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
