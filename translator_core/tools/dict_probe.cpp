// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Developer probe for the JMdict SQLite database.
//
//   dict_probe <jmdict.db> exact <word>
//   dict_probe <jmdict.db> at <text> <byte-offset>
#include <trans/DictLookup.hpp>
#include <trans/TextProcessor.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

void PrintEntries(const std::vector<trans::DictEntry> &entries)
{
	for (const trans::DictEntry &entry : entries)
	{
		std::printf("%s [%s] common=%d\n", entry.surface.c_str(), entry.reading.c_str(),
			entry.common ? 1 : 0);
		for (const trans::DictGloss &gloss : entry.glosses)
			std::printf("  (%s) %s\n", gloss.part_of_speech.c_str(), gloss.text.c_str());
	}
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		std::fprintf(stderr, "usage: %s <jmdict.db> exact <word> | at <text> <byte-offset>\n", argv[0]);
		return 2;
	}
	trans::JmDict dictionary;
	std::string error;
	if (!dictionary.open(argv[1], &error))
	{
		std::fprintf(stderr, "open failed: %s\n", error.c_str());
		return 1;
	}
	std::printf("schema=%s\n", dictionary.schemaVersion().c_str());

	if (std::strcmp(argv[2], "exact") == 0 && argc >= 4)
	{
		const trans::DictResult result = dictionary.lookupExact(argv[3]);
		PrintEntries(result.entries);
		return result.found ? 0 : 1;
	}
	if (std::strcmp(argv[2], "at") == 0 && argc >= 5)
	{
		const std::vector<trans::DictEntry> entries =
			dictionary.lookupAt(argv[3], static_cast<size_t>(std::strtoul(argv[4], nullptr, 10)));
		PrintEntries(entries);
		return entries.empty() ? 1 : 0;
	}
	std::fprintf(stderr, "unknown command\n");
	return 2;
}
