// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/shortcuts.h"

#include <QCoreApplication>

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

	std::printf("PASS shortcut parsing, validation and conflicts\n");
	return 0;
}
