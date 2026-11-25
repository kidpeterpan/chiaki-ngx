// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Stand-in for QmlBackend in the QML tests. window.ZoomFactor is left out: a QML
// property name cannot start with a capital letter, and the overlay copes with undefined.
pragma Singleton
import QtQuick

QtObject {
    property var session: ({})
    property QtObject window: QtObject {
        property int videoMode: 0
        function grabInput() {}
        function releaseInput() {}
        function setTranslatorTouchRegion(x, y, w, h, enabled) {}
    }
}
