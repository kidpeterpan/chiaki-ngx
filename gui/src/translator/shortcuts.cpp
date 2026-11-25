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

QString shortcutSetError(const QStringList &values)
{
	QStringList normalized;
	for (const QString &value : values)
	{
		if (!isValidShortcut(value))
			return QStringLiteral("Invalid shortcut: %1").arg(value);
		normalized.push_back(normalizeShortcut(value));
	}
	for (int left = 0; left < normalized.size(); ++left)
		for (int right = left + 1; right < normalized.size(); ++right)
			if (shortcutsConflict(normalized[left], normalized[right]))
				return QStringLiteral("Shortcut is used by more than one action: %1")
					.arg(normalized[left]);
	return {};
}

} // namespace translator
