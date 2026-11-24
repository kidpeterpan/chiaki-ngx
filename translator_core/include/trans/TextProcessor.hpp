// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Unicode text handling, duplicate suppression, dialogue context, and prompts.
#pragma once

#include <trans/Types.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace trans {

std::u32string utf8ToU32(std::string_view text);
std::string u32ToUtf8(std::u32string_view text);

// Folds full-width ASCII to ASCII, turns U+3000 and control whitespace into a
// single space, collapses runs, and trims. Safe on malformed UTF-8.
std::string normalizeText(std::string_view text);

size_t levenshteinDistance(std::u32string_view a, std::u32string_view b);

// 0..1 similarity after normalization and punctuation stripping.
double textSimilarity(std::string_view a, std::string_view b);

// False for empty, punctuation-only, symbol-only, or numeric-only OCR noise.
bool isTranslatableText(std::string_view text);

bool containsKana(std::string_view text);
bool containsKanji(std::string_view text);

struct DedupConfig
{
	double similarity_threshold = 0.9;
	size_t max_entries = 16;
	int64_t ttl_ms = 30000;
};

// Remembers recently translated source text and suppresses repeats. Entries
// expire so a line repeated later in a cutscene is translated again.
class TextDedupCache
{
public:
	explicit TextDedupCache(DedupConfig config = {});

	bool isDuplicate(std::string_view text, int64_t now_ms);
	void remember(std::string_view text, int64_t now_ms);
	void clear();
	size_t size() const { return entries_.size(); }

private:
	struct Entry
	{
		std::string normalized;
		std::u32string codepoints;
		int64_t timestamp_ms = 0;
	};

	void purge(int64_t now_ms);

	DedupConfig config_;
	std::deque<Entry> entries_;
};

struct ContextPair
{
	std::string source;
	std::string translation;
};

// Fixed-size rolling window of recent dialogue used to keep pronouns and
// character voice consistent across consecutive subtitle lines.
class ConversationContext
{
public:
	explicit ConversationContext(size_t max_pairs = 3);

	void add(std::string source, std::string translation);
	void clear();
	const std::deque<ContextPair> &entries() const { return entries_; }
	std::vector<ContextPair> pairs() const;

private:
	size_t max_pairs_;
	std::deque<ContextPair> entries_;
};

struct PromptConfig
{
	std::string source_language = "Japanese";
	std::string target_language = "Thai";
};

std::string buildSystemPrompt(const PromptConfig &config);
std::string buildUserPrompt(std::string_view text, const ConversationContext &context,
		const PromptConfig &config);

// Sorts boxes top-to-bottom, left-to-right, drops duplicate text, and joins
// separate lines with '\n' so the OCR result reads like the on-screen text.
std::string joinOcrLines(const std::vector<OcrLine> &lines);

} // namespace trans
