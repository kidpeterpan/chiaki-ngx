// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Pure parsing/validation helpers for user-configurable translator shortcuts.
#pragma once

#include <QString>

namespace translator {

// Empty is allowed and means "disabled". Returns an empty string when the
// input cannot be parsed as a Qt key sequence.
QString normalizeShortcut(const QString &text);
bool isValidShortcut(const QString &text);
bool shortcutsConflict(const QString &left, const QString &right);

} // namespace translator
