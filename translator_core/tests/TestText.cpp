// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/TextProcessor.hpp>

#include <string>
#include <vector>

using namespace trans;

TEST(text_utf8_roundtrip_preserves_japanese_and_emoji)
{
	const std::string original = "こんにちは、世界！🎮 PS5";
	const std::u32string codepoints = utf8ToU32(original);
	ASSERT_EQ(codepoints.size(), static_cast<size_t>(14));
	ASSERT_EQ(u32ToUtf8(codepoints), original);
}

TEST(text_utf8_invalid_bytes_become_replacement_character)
{
	const std::string broken = "a\xff\xfe" "b";
	const std::u32string decoded = utf8ToU32(broken);
	ASSERT_EQ(decoded.size(), static_cast<size_t>(4));
	ASSERT_EQ(decoded[1], U'\uFFFD');
	ASSERT_EQ(decoded[2], U'\uFFFD');
}

TEST(text_normalization_folds_fullwidth_and_collapses_space)
{
	ASSERT_EQ(normalizeText("ＡＢＣ　１２３"), "ABC 123");
	ASSERT_EQ(normalizeText("  hello \n\t world  "), "hello world");
	ASSERT_EQ(normalizeText("ゲーム\n\nを　続ける"), "ゲーム を 続ける");
}

TEST(text_levenshtein_counts_unicode_codepoints)
{
	ASSERT_EQ(levenshteinDistance(U"こんにちは", U"こんにちは"), static_cast<size_t>(0));
	ASSERT_EQ(levenshteinDistance(U"こんにちは", U"こんばんは"), static_cast<size_t>(2));
	ASSERT_EQ(levenshteinDistance(U"", U"abc"), static_cast<size_t>(3));
	ASSERT_EQ(levenshteinDistance(U"abc", U""), static_cast<size_t>(3));
}

TEST(text_similarity_ignores_punctuation_and_width)
{
	ASSERT_NEAR(textSimilarity("こんにちは！", "こんにちは"), 1.0, 1e-9);
	ASSERT_NEAR(textSimilarity("ＡＢＣ", "ABC"), 1.0, 1e-9);
	ASSERT_TRUE(textSimilarity("こんにちは", "さようなら") < 0.4);
}

TEST(text_translatable_rejects_noise)
{
	ASSERT_FALSE(isTranslatableText(""));
	ASSERT_FALSE(isTranslatableText("   "));
	ASSERT_FALSE(isTranslatableText("..."));
	ASSERT_FALSE(isTranslatableText("♪♪♪"));
	ASSERT_FALSE(isTranslatableText("12345"));
	ASSERT_TRUE(isTranslatableText("こんにちは"));
	ASSERT_TRUE(isTranslatableText("Hello there"));
	ASSERT_TRUE(isTranslatableText("PS5で遊ぼう"));
	ASSERT_TRUE(isTranslatableText("死"));
}

TEST(text_dedup_cache_skips_exact_and_near_duplicates)
{
	TextDedupCache cache{DedupConfig{0.9, 8, 30000}};
	ASSERT_FALSE(cache.isDuplicate("こんにちは", 1000));
	cache.remember("こんにちは", 1000);
	ASSERT_TRUE(cache.isDuplicate("こんにちは", 1500));
	ASSERT_TRUE(cache.isDuplicate("こんにちは！", 2000));

	const std::string long_text = "これはとても長いテスト用の文章です、ゲームの字幕です";
	std::u32string near_codepoints = utf8ToU32(long_text);
	near_codepoints[5] = U'あ';
	const std::string near = u32ToUtf8(near_codepoints);
	cache.remember(long_text, 3000);
	ASSERT_TRUE(cache.isDuplicate(near, 3000));
}

TEST(text_dedup_cache_distinguishes_unrelated_text_and_expires)
{
	TextDedupCache cache{DedupConfig{0.9, 8, 30000}};
	cache.remember("こんにちは", 1000);
	ASSERT_FALSE(cache.isDuplicate("まったく違う文章です", 1500));
	ASSERT_TRUE(cache.isDuplicate("こんにちは", 2000));
	ASSERT_FALSE(cache.isDuplicate("こんにちは", 1000 + 30001));
}

TEST(text_conversation_context_keeps_last_three_pairs)
{
	ConversationContext context(3);
	context.add("一", "one");
	context.add("二", "two");
	context.add("三", "three");
	context.add("四", "four");

	const std::vector<ContextPair> pairs = context.pairs();
	ASSERT_EQ(pairs.size(), static_cast<size_t>(3));
	ASSERT_EQ(pairs[0].source, "二");
	ASSERT_EQ(pairs[1].source, "三");
	ASSERT_EQ(pairs[2].source, "四");
}

TEST(text_join_ocr_lines_orders_by_position_and_deduplicates)
{
	std::vector<OcrLine> lines(3);
	lines[0].text = "下の行";
	lines[0].box = Rect{10, 100, 200, 30};
	lines[1].text = "上の行";
	lines[1].box = Rect{10, 10, 200, 30};
	lines[2].text = "上の行";
	lines[2].box = Rect{10, 50, 200, 30};

	ASSERT_EQ(joinOcrLines(lines), "上の行\n下の行");
}

TEST(text_prompt_contains_context_languages_and_source)
{
	ConversationContext context(3);
	context.add("おはよう", "อรุณสวัสดิ์");

	PromptConfig config;
	config.source_language = "Japanese";
	config.target_language = "Thai";

	const std::string system = buildSystemPrompt(config);
	ASSERT_TRUE(system.find("Japanese") != std::string::npos);
	ASSERT_TRUE(system.find("Thai") != std::string::npos);
	ASSERT_TRUE(system.find("subtitle") != std::string::npos || system.find("Subtitle") != std::string::npos);

	const std::string user = buildUserPrompt("元気ですか", context, config);
	ASSERT_TRUE(user.find("おはよう") != std::string::npos);
	ASSERT_TRUE(user.find("อรุณสวัสดิ์") != std::string::npos);
	ASSERT_TRUE(user.find("元気ですか") != std::string::npos);
}
