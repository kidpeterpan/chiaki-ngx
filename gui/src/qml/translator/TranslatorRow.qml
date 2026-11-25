// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// One settings row: a fixed-width label, the controls declared inside it, and an
// optional hint underneath. Keeps every row the same height and alignment.
import QtQuick
import QtQuick.Layouts

Item {
    id: row

    property string label: ""
    property string hint: ""
    property TranslatorTheme theme: TranslatorTheme {}
    default property alias content: slot.data

    width: parent ? parent.width : implicitWidth
    implicitWidth: layout.implicitWidth
    implicitHeight: layout.implicitHeight

    ColumnLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 2

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: row.theme.rowHeight
            spacing: row.theme.gap

            Text {
                visible: row.label.length > 0
                Layout.preferredWidth: row.theme.labelWidth
                text: row.label
                color: row.theme.text
                font.pixelSize: row.theme.bodySize
                elide: Text.ElideRight
            }

            RowLayout {
                id: slot
                Layout.fillWidth: true
                spacing: row.theme.gap
            }
        }

        Text {
            visible: row.hint.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: row.label.length > 0 ? row.theme.labelWidth + row.theme.gap : 0
            text: row.hint
            color: row.theme.textMuted
            font.pixelSize: row.theme.captionSize
            wrapMode: Text.Wrap
        }
    }
}
