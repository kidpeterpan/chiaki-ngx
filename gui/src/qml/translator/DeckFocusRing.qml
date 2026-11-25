// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Focus ring drawn around a translator control when it was reached with the D-pad or
// keyboard. Touch and mouse focus stay unmarked.
import QtQuick

Rectangle {
    // A Control or TextField; both expose activeFocus and focusReason.
    required property var target
    property TranslatorTheme theme: TranslatorTheme {}

    anchors.fill: parent
    anchors.margins: -3
    z: 100
    radius: 8
    color: "transparent"
    border.width: 3
    border.color: theme.focusRing
    visible: target.activeFocus && target.focusReason !== Qt.MouseFocusReason
}
