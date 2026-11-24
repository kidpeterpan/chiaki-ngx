// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/DictLookup.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

namespace trans {
namespace {

struct DecodedCodepoint
{
	char32_t value = 0;
	size_t begin = 0;
	size_t end = 0;
};

std::vector<DecodedCodepoint> decodeWithOffsets(std::string_view text)
{
	std::vector<DecodedCodepoint> decoded;
	decoded.reserve(text.size());
	size_t index = 0;
	while (index < text.size())
	{
		const unsigned char lead = static_cast<unsigned char>(text[index]);
		if (lead < 0x80)
		{
			decoded.push_back(DecodedCodepoint{lead, index, index + 1});
			++index;
			continue;
		}

		int length = 0;
		char32_t value = 0;
		if ((lead & 0xE0) == 0xC0)
		{
			length = 2;
			value = lead & 0x1F;
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			length = 3;
			value = lead & 0x0F;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			length = 4;
			value = lead & 0x07;
		}
		else
		{
			decoded.push_back(DecodedCodepoint{0xFFFD, index, index + 1});
			++index;
			continue;
		}

		if (index + static_cast<size_t>(length) > text.size())
		{
			decoded.push_back(DecodedCodepoint{0xFFFD, index, index + 1});
			++index;
			continue;
		}
		bool valid = true;
		for (int offset = 1; offset < length; ++offset)
		{
			const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
			if ((continuation & 0xC0) != 0x80)
			{
				valid = false;
				break;
			}
			value = (value << 6) | (continuation & 0x3F);
		}
		if (!valid)
		{
			decoded.push_back(DecodedCodepoint{0xFFFD, index, index + 1});
			++index;
			continue;
		}
		decoded.push_back(DecodedCodepoint{value, index, index + static_cast<size_t>(length)});
		index += static_cast<size_t>(length);
	}
	return decoded;
}

JpTokenType classify(char32_t codepoint)
{
	if ((codepoint >= 0x3040 && codepoint <= 0x309F) ||
			(codepoint >= 0x31F0 && codepoint <= 0x31FF))
		return JpTokenType::Hiragana;
	if ((codepoint >= 0x30A0 && codepoint <= 0x30FF) ||
			(codepoint >= 0xFF66 && codepoint <= 0xFF9D))
		return JpTokenType::Katakana;
	if ((codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
			(codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
			(codepoint >= 0xF900 && codepoint <= 0xFAFF))
		return JpTokenType::Kanji;
	if ((codepoint >= U'A' && codepoint <= U'Z') || (codepoint >= U'a' && codepoint <= U'z') ||
			(codepoint >= 0x00C0 && codepoint <= 0x024F))
		return JpTokenType::Latin;
	if (codepoint >= U'0' && codepoint <= U'9')
		return JpTokenType::Digit;
	return JpTokenType::Other;
}

bool isWordType(JpTokenType type)
{
	return type != JpTokenType::Other;
}

std::string escapeLike(std::string_view value)
{
	std::string escaped;
	escaped.reserve(value.size() + 4);
	for (char character : value)
	{
		if (character == '\\' || character == '%' || character == '_')
			escaped.push_back('\\');
		escaped.push_back(character);
	}
	return escaped;
}

void appendUnique(std::vector<DictEntry> &target, DictEntry entry)
{
	for (const DictEntry &existing : target)
	{
		if (existing.entry_id == entry.entry_id && existing.surface == entry.surface)
			return;
	}
	target.push_back(std::move(entry));
}

std::string ColumnText(sqlite3_stmt *statement, int column)
{
	const unsigned char *text = sqlite3_column_text(statement, column);
	return text ? reinterpret_cast<const char *>(text) : std::string();
}

} // namespace

std::vector<JpToken> tokenizeJapanese(std::string_view text)
{
	const std::vector<DecodedCodepoint> decoded = decodeWithOffsets(text);
	std::vector<JpToken> tokens;
	for (const DecodedCodepoint &codepoint : decoded)
	{
		const JpTokenType type = classify(codepoint.value);
		if (!tokens.empty() && tokens.back().type == type && isWordType(type) &&
				tokens.back().end == codepoint.begin)
		{
			tokens.back().end = codepoint.end;
			tokens.back().surface = std::string(text.substr(tokens.back().begin,
				tokens.back().end - tokens.back().begin));
			continue;
		}
		tokens.push_back(JpToken{std::string(text.substr(codepoint.begin,
			codepoint.end - codepoint.begin)), codepoint.begin, codepoint.end, type});
	}
	return tokens;
}

std::vector<std::string> deinflectionCandidates(std::string_view word)
{
	std::vector<std::string> candidates;
	if (word.empty())
		return candidates;
	candidates.emplace_back(word);

	struct Rule
	{
		const char *suffix;
		const char *replacement;
	};
	// Longest suffixes first: かった must win over た.
	static const Rule rules[] = {
		{"ませんでした", "る"},
		{"ません", "る"},
		{"ました", "る"},
		{"ます", "る"},
		{"なかった", "る"},
		{"ない", "る"},
		{"られる", "る"},
		{"させる", "る"},
		{"なさい", "る"},
		{"れば", "る"},
		{"よう", "る"},
		{"たい", "る"},
		{"かった", "い"},
		{"くない", "い"},
		{"かったら", "い"},
		{"った", "う"},
		{"って", "う"},
		{"わない", "う"},
		{"いた", "く"},
		{"いて", "く"},
		{"いだ", "ぐ"},
		{"いで", "ぐ"},
		{"した", "す"},
		{"して", "す"},
		{"んだ", "む"},
		{"んで", "む"},
		{"んだ", "ぶ"},
		{"んで", "ぶ"},
		{"んだ", "ぬ"},
		{"んで", "ぬ"},
		{"った", "つ"},
		{"って", "つ"},
		{"った", "る"},
		{"って", "る"},
		{"た", "る"},
		{"て", "る"},
		{"ず", "る"},
	};

	for (const Rule &rule : rules)
	{
		const size_t suffix_length = std::strlen(rule.suffix);
		if (word.size() <= suffix_length)
			continue;
		if (word.compare(word.size() - suffix_length, suffix_length, rule.suffix) != 0)
			continue;
		std::string candidate(word.substr(0, word.size() - suffix_length));
		candidate += rule.replacement;
		if (std::find(candidates.begin(), candidates.end(), candidate) == candidates.end())
			candidates.push_back(std::move(candidate));
	}
	return candidates;
}

struct JmDict::Impl
{
	sqlite3 *db = nullptr;
	mutable std::mutex mutex;
	std::string schema_version;

	bool ready() const { return db != nullptr; }

	void close()
	{
		if (db)
		{
			sqlite3_close(db);
			db = nullptr;
		}
	}

	bool loadGlossesLocked(DictEntry &entry) const
	{
		sqlite3_stmt *statement = nullptr;
		if (sqlite3_prepare_v2(db,
				"SELECT sense_order, pos, text, lang FROM glosses WHERE entry_id = ?1 "
				"ORDER BY sense_order, rowid LIMIT 8",
				-1, &statement, nullptr) != SQLITE_OK)
			return false;
		sqlite3_bind_int64(statement, 1, entry.entry_id);
		while (sqlite3_step(statement) == SQLITE_ROW)
		{
			DictGloss gloss;
			gloss.part_of_speech = ColumnText(statement, 1);
			gloss.text = ColumnText(statement, 2);
			gloss.language = ColumnText(statement, 3);
			entry.glosses.push_back(std::move(gloss));
		}
		sqlite3_finalize(statement);
		return true;
	}

	DictResult lookupExactLocked(std::string_view word) const
	{
		DictResult result;
		if (!db)
		{
			result.error = "dictionary is not open";
			return result;
		}
		sqlite3_stmt *statement = nullptr;
		if (sqlite3_prepare_v2(db,
				"SELECT entry_id, surface, reading, common FROM words "
				"WHERE surface = ?1 ORDER BY common DESC, entry_id LIMIT 20",
				-1, &statement, nullptr) != SQLITE_OK)
		{
			result.error = "dictionary query failed";
			return result;
		}
		const std::string word_copy(word);
		sqlite3_bind_text(statement, 1, word_copy.c_str(), -1, SQLITE_TRANSIENT);
		while (sqlite3_step(statement) == SQLITE_ROW)
		{
			DictEntry entry;
			entry.entry_id = sqlite3_column_int64(statement, 0);
			entry.surface = ColumnText(statement, 1);
			entry.reading = ColumnText(statement, 2);
			entry.common = sqlite3_column_int(statement, 3) != 0;
			appendUnique(result.entries, std::move(entry));
		}
		sqlite3_finalize(statement);
		for (DictEntry &entry : result.entries)
			loadGlossesLocked(entry);
		result.found = !result.entries.empty();
		return result;
	}

	std::vector<DictEntry> lookupPrefixLocked(std::string_view prefix, size_t limit) const
	{
		std::vector<DictEntry> entries;
		if (!db || prefix.empty() || limit == 0)
			return entries;
		sqlite3_stmt *statement = nullptr;
		if (sqlite3_prepare_v2(db,
				"SELECT entry_id, surface, reading, common FROM words "
				"WHERE surface LIKE ?1 ESCAPE '\\' "
				"ORDER BY common DESC, length(surface) ASC, surface ASC LIMIT ?2",
				-1, &statement, nullptr) != SQLITE_OK)
			return entries;
		const std::string pattern = escapeLike(prefix) + "%";
		sqlite3_bind_text(statement, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
		sqlite3_bind_int64(statement, 2, static_cast<sqlite3_int64>(limit));
		while (sqlite3_step(statement) == SQLITE_ROW)
		{
			DictEntry entry;
			entry.entry_id = sqlite3_column_int64(statement, 0);
			entry.surface = ColumnText(statement, 1);
			entry.reading = ColumnText(statement, 2);
			entry.common = sqlite3_column_int(statement, 3) != 0;
			appendUnique(entries, std::move(entry));
		}
		sqlite3_finalize(statement);
		for (DictEntry &entry : entries)
			loadGlossesLocked(entry);
		return entries;
	}

	std::vector<DictEntry> lookupAtLocked(std::string_view text, size_t byte_offset,
			size_t limit) const
	{
		std::vector<DictEntry> entries;
		if (!db || limit == 0 || text.empty() || byte_offset >= text.size())
			return entries;

		const std::vector<DecodedCodepoint> codepoints = decodeWithOffsets(text);
		const std::vector<JpToken> tokens = tokenizeJapanese(text);
		if (tokens.empty())
			return entries;

		size_t tap = codepoints.size();
		for (size_t index = 0; index < codepoints.size(); ++index)
		{
			if (codepoints[index].begin <= byte_offset && byte_offset < codepoints[index].end)
			{
				tap = index;
				break;
			}
		}
		if (tap == codepoints.size())
			return entries;

		int token_index = -1;
		for (size_t index = 0; index < tokens.size(); ++index)
		{
			if (tokens[index].begin <= byte_offset && byte_offset < tokens[index].end)
			{
				token_index = static_cast<int>(index);
				break;
			}
		}
		if (token_index < 0 || !isWordType(tokens[static_cast<size_t>(token_index)].type))
			return entries;

		constexpr size_t kMaxWindowBytes = 48;
		size_t window_begin = static_cast<size_t>(token_index);
		size_t window_end = static_cast<size_t>(token_index);
		size_t window_bytes = tokens[window_begin].surface.size();
		while (window_begin > 0)
		{
			const JpToken &previous = tokens[window_begin - 1];
			if (!isWordType(previous.type) || window_bytes + previous.surface.size() > kMaxWindowBytes)
				break;
			--window_begin;
			window_bytes += previous.surface.size();
		}
		while (window_end + 1 < tokens.size())
		{
			const JpToken &next = tokens[window_end + 1];
			if (!isWordType(next.type) || window_bytes + next.surface.size() > kMaxWindowBytes)
				break;
			++window_end;
			window_bytes += next.surface.size();
		}

		size_t begin_index = 0;
		size_t end_index = codepoints.size();
		for (size_t index = 0; index < codepoints.size(); ++index)
		{
			if (codepoints[index].begin == tokens[window_begin].begin)
				begin_index = index;
			if (codepoints[index].end == tokens[window_end].end)
			{
				end_index = index + 1;
				break;
			}
		}
		if (begin_index >= codepoints.size() || end_index <= begin_index)
			return entries;
		const size_t window_count = end_index - begin_index;
		const size_t tap_in_window = tap - begin_index;
		if (tap_in_window >= window_count)
			return entries;

		std::set<std::string> tried;
		for (size_t length = std::min<size_t>(window_count, 16); length >= 1; --length)
		{
			const size_t first_start = tap_in_window + 1 >= length ? tap_in_window + 1 - length : 0;
			const size_t last_start = std::min(tap_in_window, window_count - length);
			for (size_t start = first_start; start <= last_start; ++start)
			{
				const size_t begin_byte = codepoints[begin_index + start].begin;
				const size_t end_byte = codepoints[begin_index + start + length - 1].end;
				const std::string candidate = std::string(text.substr(begin_byte, end_byte - begin_byte));
				if (!tried.insert(candidate).second)
					continue;

				DictResult exact = lookupExactLocked(candidate);
				if (exact.found)
				{
					for (DictEntry &entry : exact.entries)
						appendUnique(entries, std::move(entry));
				}
				else
				{
					for (const std::string &deinflected : deinflectionCandidates(candidate))
					{
						if (deinflected == candidate)
							continue;
						DictResult inflected = lookupExactLocked(deinflected);
						if (!inflected.found)
							continue;
						for (DictEntry &entry : inflected.entries)
							appendUnique(entries, std::move(entry));
					}
				}
				if (entries.size() >= limit)
					return entries;
			}
		}

		if (entries.empty())
		{
			const JpToken &token = tokens[static_cast<size_t>(token_index)];
			const std::vector<DictEntry> suggestions = lookupPrefixLocked(token.surface, limit);
			for (const DictEntry &entry : suggestions)
				appendUnique(entries, entry);
		}
		return entries;
	}
};

JmDict::JmDict() : impl_(std::make_unique<Impl>()) {}

JmDict::~JmDict()
{
	close();
}

bool JmDict::open(const std::string &path, std::string *error)
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->close();
	if (sqlite3_open_v2(path.c_str(), &impl_->db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
	{
		if (error)
			*error = impl_->db ? sqlite3_errmsg(impl_->db) : "cannot open dictionary";
		impl_->close();
		return false;
	}

	const char *required[] = {"entries", "words", "glosses"};
	for (const char *table : required)
	{
		sqlite3_stmt *statement = nullptr;
		const std::string query = std::string("SELECT 1 FROM ") + table + " LIMIT 1";
		if (sqlite3_prepare_v2(impl_->db, query.c_str(), -1, &statement, nullptr) != SQLITE_OK)
		{
			if (error)
				*error = std::string("not a JMdict database (missing table: ") + table + ")";
			sqlite3_finalize(statement);
			impl_->close();
			return false;
		}
		sqlite3_finalize(statement);
	}

	impl_->schema_version = "unknown";
	sqlite3_stmt *statement = nullptr;
	if (sqlite3_prepare_v2(impl_->db, "SELECT value FROM meta WHERE key = 'schema_version'",
			-1, &statement, nullptr) == SQLITE_OK)
	{
		if (sqlite3_step(statement) == SQLITE_ROW)
			impl_->schema_version = ColumnText(statement, 0);
		sqlite3_finalize(statement);
	}
	if (error)
		error->clear();
	return true;
}

void JmDict::close()
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	impl_->close();
}

bool JmDict::ready() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->ready();
}

std::string JmDict::schemaVersion() const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->schema_version;
}

DictResult JmDict::lookupExact(std::string_view word) const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->lookupExactLocked(word);
}

std::vector<DictEntry> JmDict::lookupPrefix(std::string_view prefix, size_t limit) const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->lookupPrefixLocked(prefix, limit);
}

std::vector<DictEntry> JmDict::lookupAt(std::string_view text, size_t byte_offset, size_t limit) const
{
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->lookupAtLocked(text, byte_offset, limit);
}

} // namespace trans
