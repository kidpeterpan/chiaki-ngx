// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Design tokens for the translator UI. Sizes target the Steam Deck: 1280x800 on a
// 7" screen, used at arm's length with thumbs, so rows and text are larger than desktop.
import QtQuick

QtObject {
    readonly property color panel: "#f00e1014"
    readonly property color panelBorder: "#55ffffff"
    readonly property color divider: "#33ffffff"
    readonly property color chip: "#14ffffff"
    readonly property color textStrong: "white"
    readonly property color text: "#dddddd"
    readonly property color textMuted: "#a8ffffff"
    readonly property color accent: "#e6007acc"
    readonly property color launcher: "#99000000"
    readonly property color focusRing: "#ffd27a"
    readonly property color ok: "#7bd88f"
    readonly property color warning: "#ffd27a"
    readonly property color error: "#ff8a80"

    readonly property int titleSize: 22
    readonly property int bodySize: 16
    readonly property int captionSize: 13

    readonly property int rowHeight: 48
    readonly property int labelWidth: 170
    readonly property int gap: 10
    readonly property int radius: 12
    readonly property int launcherSize: 48
}
