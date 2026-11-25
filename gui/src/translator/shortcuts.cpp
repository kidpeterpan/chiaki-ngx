// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/shortcuts.h"

#include <QKeySequence>

namespace translator {

QString normalizeShortcut(const QString &text)
{
	const QString trimmed = text.trimmed();
	if (trimmed.isEmpty())
		return {};
	const QKeySequence sequence = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
	if (sequence.isEmpty())
		return {};
	return sequence.toString(QKeySequence::PortableText);
}

bool isValidShortcut(const QString &text)
{
	return text.trimmed().isEmpty() || !normalizeShortcut(text).isEmpty();
}

bool shortcutsConflict(const QString &left, const QString &right)
{
	const QString a = normalizeShortcut(left);
	const QString b = normalizeShortcut(right);
	return !a.isEmpty() && a == b;
}

} // namespace translator
