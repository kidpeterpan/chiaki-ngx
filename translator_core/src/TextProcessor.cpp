// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/TextProcessor.hpp>

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace trans {
namespace {

bool isWhitespace(char32_t cp)
{
	return cp == U' ' || cp == U'\t' || cp == U'\n' || cp == U'\r' || cp == U'\v' || cp == U'\f' ||
		cp == 0x3000 || cp == 0x00A0;
}

bool isControl(char32_t cp)
{
	return cp < 0x20 || cp == 0x7F;
}

bool isComparisonPunctuation(char32_t cp)
{
	if (cp < 0x80)
	{
		if ((cp >= U'!' && cp <= U'/') || (cp >= U':' && cp <= U'@') ||
				(cp >= U'[' && cp <= U'`') || (cp >= U'{' && cp <= U'~'))
			return true;
		return false;
	}
	switch (cp)
	{
	case 0x3001: // 、
	case 0x3002: // 。
	case 0x3005: // 々 (iteration mark, handled as text elsewhere)
		return cp == 0x3001 || cp == 0x3002;
	case 0x300C:
	case 0x300D:
	case 0x300E:
	case 0x300F:
	case 0x3010:
	case 0x3011:
	case 0x3014:
	case 0x3015:
	case 0x3016:
	case 0x3017:
	case 0x2026: // …
	case 0x2025: // ‥
	case 0x30FB: // ・
	case 0x301C: // 〜
	case 0x2010:
	case 0x2011:
	case 0x2012:
	case 0x2013:
	case 0x2014:
	case 0x2018:
	case 0x2019:
	case 0x201C:
	case 0x201D:
	case 0x00B7:
		return true;
	default:
		return false;
	}
}

bool isSemantic(char32_t cp)
{
	if ((cp >= U'0' && cp <= U'9'))
		return false;
	if ((cp >= U'A' && cp <= U'Z') || (cp >= U'a' && cp <= U'z'))
		return true;
	// Hiragana, katakana, kanji, half-width katakana, hangul, Thai.
	if ((cp >= 0x3040 && cp <= 0x30FF) || (cp >= 0x31F0 && cp <= 0x31FF) ||
			(cp >= 0xFF66 && cp <= 0xFF9D) || (cp >= 0x3400 && cp <= 0x4DBF) ||
			(cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
			(cp >= 0xAC00 && cp <= 0xD7AF) || (cp >= 0x0E00 && cp <= 0x0E7F) ||
			(cp >= 0x00C0 && cp <= 0x024F))
		return true;
	return false;
}

std::u32string stripForComparison(const std::u32string &text)
{
	std::u32string stripped;
	stripped.reserve(text.size());
	for (char32_t cp : text)
	{
		if (isWhitespace(cp) || isComparisonPunctuation(cp))
			continue;
		stripped.push_back(cp);
	}
	return stripped;
}

double similarityU32(const std::u32string &a, const std::u32string &b)
{
	const size_t longest = std::max(a.size(), b.size());
	if (longest == 0)
		return 1.0;
	const size_t distance = levenshteinDistance(a, b);
	return 1.0 - static_cast<double>(distance) / static_cast<double>(longest);
}

} // namespace

std::u32string utf8ToU32(std::string_view text)
{
	std::u32string out;
	out.reserve(text.size());
	size_t index = 0;
	while (index < text.size())
	{
		const unsigned char lead = static_cast<unsigned char>(text[index]);
		if (lead < 0x80)
		{
			out.push_back(static_cast<char32_t>(lead));
			++index;
			continue;
		}

		int length = 0;
		char32_t codepoint = 0;
		if ((lead & 0xE0) == 0xC0)
		{
			length = 2;
			codepoint = lead & 0x1F;
		}
		else if ((lead & 0xF0) == 0xE0)
		{
			length = 3;
			codepoint = lead & 0x0F;
		}
		else if ((lead & 0xF8) == 0xF0)
		{
			length = 4;
			codepoint = lead & 0x07;
		}
		else
		{
			out.push_back(0xFFFD);
			++index;
			continue;
		}

		if (index + static_cast<size_t>(length) > text.size())
		{
			out.push_back(0xFFFD);
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
			codepoint = (codepoint << 6) | (continuation & 0x3F);
		}

		const bool overlong = (length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
			(length == 4 && codepoint < 0x10000);
		if (!valid || overlong || codepoint > 0x10FFFF ||
				(codepoint >= 0xD800 && codepoint <= 0xDFFF))
		{
			out.push_back(0xFFFD);
			++index;
			continue;
		}

		out.push_back(codepoint);
		index += static_cast<size_t>(length);
	}
	return out;
}

std::string u32ToUtf8(std::u32string_view text)
{
	std::string out;
	out.reserve(text.size() * 3);
	for (char32_t codepoint : text)
	{
		if (codepoint <= 0x7F)
		{
			out.push_back(static_cast<char>(codepoint));
		}
		else if (codepoint <= 0x7FF)
		{
			out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
		else if (codepoint <= 0xFFFF)
		{
			out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
		else
		{
			out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
		}
	}
	return out;
}

std::string normalizeText(std::string_view text)
{
	const std::u32string codepoints = utf8ToU32(text);
	std::string out;
	out.reserve(text.size());
	bool pending_space = false;
	bool started = false;

	for (char32_t codepoint : codepoints)
	{
		if (codepoint >= 0xFF01 && codepoint <= 0xFF5E)
			codepoint -= 0xFEE0;
		else if (codepoint == 0x3000)
			codepoint = U' ';

		if (isControl(codepoint))
			codepoint = U' ';

		if (isWhitespace(codepoint))
		{
			pending_space = started;
			continue;
		}

		if (pending_space)
		{
			out.push_back(' ');
			pending_space = false;
		}
		started = true;

		if (codepoint <= 0x7F)
			out.push_back(static_cast<char>(codepoint));
		else
			out += u32ToUtf8(std::u32string(1, codepoint));
	}
	return out;
}

size_t levenshteinDistance(std::u32string_view a, std::u32string_view b)
{
	if (a.size() < b.size())
		std::swap(a, b);

	const size_t short_length = b.size();
	std::vector<size_t> previous(short_length + 1);
	std::vector<size_t> current(short_length + 1);
	for (size_t index = 0; index <= short_length; ++index)
		previous[index] = index;

	for (size_t i = 1; i <= a.size(); ++i)
	{
		current[0] = i;
		for (size_t j = 1; j <= short_length; ++j)
		{
			const size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
			current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
		}
		std::swap(previous, current);
	}
	return previous[short_length];
}

double textSimilarity(std::string_view a, std::string_view b)
{
	const std::u32string left = stripForComparison(utf8ToU32(normalizeText(a)));
	const std::u32string right = stripForComparison(utf8ToU32(normalizeText(b)));
	if (left.empty() && right.empty())
		return normalizeText(a) == normalizeText(b) ? 1.0 : 0.0;
	return similarityU32(left, right);
}

bool isTranslatableText(std::string_view text)
{
	const std::u32string codepoints = utf8ToU32(normalizeText(text));
	for (char32_t codepoint : codepoints)
	{
		if (isSemantic(codepoint))
			return true;
	}
	return false;
}

bool containsKana(std::string_view text)
{
	for (char32_t codepoint : utf8ToU32(text))
	{
		if ((codepoint >= 0x3040 && codepoint <= 0x30FF) ||
				(codepoint >= 0x31F0 && codepoint <= 0x31FF) ||
				(codepoint >= 0xFF66 && codepoint <= 0xFF9D))
			return true;
	}
	return false;
}

bool containsKanji(std::string_view text)
{
	for (char32_t codepoint : utf8ToU32(text))
	{
		if ((codepoint >= 0x3400 && codepoint <= 0x4DBF) ||
				(codepoint >= 0x4E00 && codepoint <= 0x9FFF) ||
				(codepoint >= 0xF900 && codepoint <= 0xFAFF))
			return true;
	}
	return false;
}

TextDedupCache::TextDedupCache(DedupConfig config) : config_(config) {}

void TextDedupCache::purge(int64_t now_ms)
{
	while (!entries_.empty() && now_ms - entries_.front().timestamp_ms > config_.ttl_ms)
		entries_.pop_front();
}

void TextDedupCache::remember(std::string_view text, int64_t now_ms)
{
	const std::string normalized = normalizeText(text);
	if (normalized.empty())
		return;
	purge(now_ms);
	entries_.push_back(Entry{normalized, stripForComparison(utf8ToU32(normalized)), now_ms});
	while (entries_.size() > config_.max_entries)
		entries_.pop_front();
}

bool TextDedupCache::isDuplicate(std::string_view text, int64_t now_ms)
{
	purge(now_ms);
	const std::u32string candidate = stripForComparison(utf8ToU32(normalizeText(text)));
	if (candidate.empty())
		return false;
	for (const Entry &entry : entries_)
	{
		if (entry.codepoints == candidate)
			return true;
		if (similarityU32(entry.codepoints, candidate) >= config_.similarity_threshold)
			return true;
	}
	return false;
}

void TextDedupCache::clear()
{
	entries_.clear();
}

ConversationContext::ConversationContext(size_t max_pairs) : max_pairs_(max_pairs == 0 ? 1 : max_pairs) {}

void ConversationContext::add(std::string source, std::string translation)
{
	entries_.push_back(ContextPair{std::move(source), std::move(translation)});
	while (entries_.size() > max_pairs_)
		entries_.pop_front();
}

void ConversationContext::clear()
{
	entries_.clear();
}

std::vector<ContextPair> ConversationContext::pairs() const
{
	return std::vector<ContextPair>(entries_.begin(), entries_.end());
}

std::string buildSystemPrompt(const PromptConfig &config)
{
	std::ostringstream prompt;
	prompt << "You are the subtitle translator built into a PlayStation Remote Play client.\n"
		   << "Translate " << config.source_language << " video-game subtitles into "
		   << config.target_language << ".\n"
		   << "The input comes from optical character recognition, so it may contain broken line\n"
		   << "breaks, missing spaces, or vertical-text fragments. Recover the intended sentence.\n"
		   << "Rules:\n"
		   << "- Output only the translation: no notes, no quotes, no romanization.\n"
		   << "- Keep character names and honorifics such as -san, -chan, and -sama intact, unless a\n"
		   << "  widely used " << config.target_language << " transliteration already exists.\n"
		   << "- Preserve the speaker's register, gender, and pronouns.\n"
		   << "- Keep the result short enough to read in an overlay: at most two short lines.\n"
		   << "- If the input is sound effects, menu chrome, or otherwise not dialogue, output an\n"
		   << "  empty line.\n";
	return prompt.str();
}

std::string buildUserPrompt(std::string_view text, const ConversationContext &context,
		const PromptConfig &config)
{
	std::ostringstream prompt;
	if (!context.entries().empty())
	{
		prompt << "Recent dialogue, oldest first:\n";
		for (const ContextPair &pair : context.entries())
		{
			prompt << config.source_language << ": " << pair.source << "\n";
			prompt << config.target_language << ": " << pair.translation << "\n";
		}
		prompt << "\n";
	}
	prompt << "Translate this " << config.source_language << " subtitle into "
		   << config.target_language << ". Output only the translation.\n";
	prompt << config.source_language << ": " << text << "\n";
	prompt << config.target_language << ":";
	return prompt.str();
}

std::string joinOcrLines(const std::vector<OcrLine> &lines)
{
	std::vector<const OcrLine *> ordered;
	ordered.reserve(lines.size());
	for (const OcrLine &line : lines)
	{
		const std::string normalized = normalizeText(line.text);
		if (!normalized.empty())
			ordered.push_back(&line);
	}
	std::stable_sort(ordered.begin(), ordered.end(), [](const OcrLine *a, const OcrLine *b) {
		if (a->box.y != b->box.y)
			return a->box.y < b->box.y;
		return a->box.x < b->box.x;
	});

	std::set<std::string> seen;
	std::ostringstream joined;
	bool first = true;
	for (const OcrLine *line : ordered)
	{
		const std::string normalized = normalizeText(line->text);
		if (!seen.insert(normalized).second)
			continue;
		if (!first)
			joined << '\n';
		joined << normalized;
		first = false;
	}
	return joined.str();
}

} // namespace trans
