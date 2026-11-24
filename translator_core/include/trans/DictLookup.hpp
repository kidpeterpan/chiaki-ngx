// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// JMdict lookup backed by SQLite, plus the lightweight Japanese tokenizer used
// for tap-to-lookup and deinflection candidates for inflected subtitle text.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace trans {

enum class JpTokenType
{
	Kanji,
	Hiragana,
	Katakana,
	Latin,
	Digit,
	Other,
};

struct JpToken
{
	std::string surface;
	size_t begin = 0; // byte offset in the source string
	size_t end = 0;
	JpTokenType type = JpTokenType::Other;
};

std::vector<JpToken> tokenizeJapanese(std::string_view text);

struct DictGloss
{
	std::string text;
	std::string part_of_speech;
	std::string language = "eng";
};

struct DictEntry
{
	int64_t entry_id = 0;
	std::string surface;
	std::string reading;
	bool common = false;
	std::vector<DictGloss> glosses;
};

struct DictResult
{
	bool found = false;
	std::vector<DictEntry> entries;
	std::string error;
};

// Read-only wrapper around a jmdict.db built by tools/build_jmdict.py.
class JmDict
{
public:
	JmDict();
	~JmDict();
	JmDict(const JmDict &) = delete;
	JmDict &operator=(const JmDict &) = delete;

	bool open(const std::string &path, std::string *error);
	void close();
	bool ready() const;
	std::string schemaVersion() const;

	DictResult lookupExact(std::string_view word) const;
	std::vector<DictEntry> lookupPrefix(std::string_view prefix, size_t limit = 8) const;

	// Finds dictionary entries covering the tapped byte offset. Tries longer
	// merged spans first (食べる), then deinflected forms (食べた -> 食べる).
	std::vector<DictEntry> lookupAt(std::string_view text, size_t byte_offset,
			size_t limit = 5) const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};

// Returns the original word first, then plausible dictionary forms.
std::vector<std::string> deinflectionCandidates(std::string_view word);

} // namespace trans
