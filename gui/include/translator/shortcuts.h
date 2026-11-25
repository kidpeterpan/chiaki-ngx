// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Pure parsing/validation helpers for user-configurable translator shortcuts.
#pragma once

#include <QString>
#include <QStringList>

namespace translator {

// Empty is allowed and means "disabled". Returns an empty string when the
// input cannot be parsed as a Qt key sequence.
QString normalizeShortcut(const QString &text);
bool isValidShortcut(const QString &text);
bool shortcutsConflict(const QString &left, const QString &right);
// Empty when every entry is valid and non-conflicting; otherwise a user-facing
// message. Empty entries mean "disabled" and do not conflict.
QString shortcutSetError(const QStringList &values);
// QML key-capture helper: turns one key press into a portable sequence.
QString shortcutFromKey(int key, int modifiers);

} // namespace translator
