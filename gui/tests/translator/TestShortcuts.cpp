// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/shortcuts.h"

#include <QCoreApplication>
#include <Qt>

#include <cstdio>

namespace {

int Fail(const char *message)
{
	std::fprintf(stderr, "FAIL: %s\n", message);
	return 1;
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);

	if (translator::normalizeShortcut("F13") != QStringLiteral("F13"))
		return Fail("F13 did not normalize");
	if (translator::normalizeShortcut("Ctrl+Shift+T") != QStringLiteral("Ctrl+Shift+T"))
		return Fail("modifier combination did not normalize");
	if (translator::normalizeShortcut(" ctrl + shift + t ") != QStringLiteral("Ctrl+Shift+T"))
		return Fail("spaces/case were not normalized");
	if (translator::normalizeShortcut("Shift+Ctrl+T") !=
			translator::normalizeShortcut("Ctrl+Shift+T"))
		return Fail("modifier order produced different sequences");
	if (!translator::normalizeShortcut("F13").isEmpty())
	{
		if (translator::normalizeShortcut("F13") == translator::normalizeShortcut("F14"))
			return Fail("different keys compared equal");
	}

	if (!translator::isValidShortcut(QString()))
		return Fail("empty shortcut should mean disabled");
	if (!translator::isValidShortcut("F12"))
		return Fail("F12 should be valid");
	if (translator::isValidShortcut("Ctrl+"))
		return Fail("incomplete chord was accepted");
	if (translator::isValidShortcut("definitely-not-a-key"))
		return Fail("garbage was accepted");

	if (!translator::shortcutsConflict("F13", "F13"))
		return Fail("same shortcut not detected");
	if (!translator::shortcutsConflict("ctrl+shift+t", "Ctrl+Shift+T"))
		return Fail("case-insensitive conflict not detected");
	if (translator::shortcutsConflict("F13", "F14"))
		return Fail("different shortcuts flagged as conflict");
	if (translator::shortcutsConflict("", ""))
		return Fail("empty shortcuts flagged as conflict");

	if (translator::shortcutFromKey(Qt::Key_T, Qt::ControlModifier) != QStringLiteral("Ctrl+T"))
		return Fail("key capture did not build Ctrl+T");
	if (translator::shortcutFromKey(Qt::Key_F11, Qt::NoModifier) != QStringLiteral("F11"))
		return Fail("key capture did not build F11");
	if (!translator::shortcutFromKey(Qt::Key_Control, Qt::ControlModifier).isEmpty())
		return Fail("modifier-only key capture was accepted");

	if (!translator::shortcutSetError({"F11", "F13", "F14", "F15", "F12"}).isEmpty())
		return Fail("valid shortcut set was rejected");
	if (translator::shortcutSetError({"F11", "F13", "f13", "F15", "F12"}).isEmpty())
		return Fail("duplicate shortcut set was accepted");
	const QString empty_error = translator::shortcutSetError({"F13", "F14", "F15", ""});
	if (!empty_error.isEmpty())
	{
		std::fprintf(stderr, "empty set error: %s\n", empty_error.toUtf8().constData());
		return Fail("empty (disabled) shortcut was rejected");
	}
	if (translator::shortcutSetError({"F11", "Ctrl+", "F14", "F15", "F12"}).isEmpty())
		return Fail("invalid shortcut set was accepted");

	std::printf("PASS shortcut parsing, validation and conflicts\n");
	return 0;
}
