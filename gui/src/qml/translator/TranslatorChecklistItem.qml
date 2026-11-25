// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// One card of the setup checklist: what it covers, its state with an icon (not colour
// alone), and the buttons that fix it.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card

    property string title: ""
    property string stateText: ""
    property bool ok: true
    property TranslatorTheme theme: TranslatorTheme {}
    default property alias actions: actionRow.data

    Layout.fillWidth: true
    Layout.preferredWidth: 1
    implicitHeight: layout.implicitHeight + 2 * theme.gap
    radius: 10
    color: theme.chip
    border.width: 1
    border.color: ok ? theme.divider : theme.warning

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: card.theme.gap
        spacing: 4

        Text {
            text: card.title
            color: card.theme.textMuted
            font.pixelSize: card.theme.captionSize
        }

        Text {
            Layout.fillWidth: true
            text: (card.ok ? "✓ " : "⚠ ") + card.stateText
            color: card.ok ? card.theme.ok : card.theme.warning
            font.pixelSize: card.theme.bodySize
            font.bold: true
            wrapMode: Text.Wrap
        }

        // Wraps, so two actions fit a narrow card instead of spilling into the next one.
        Flow {
            id: actionRow
            Layout.fillWidth: true
            spacing: 6
        }
    }
}
