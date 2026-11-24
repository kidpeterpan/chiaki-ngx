// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/TranslatorBackend.hpp>

#include <string>
#include <thread>
#include <vector>

using namespace trans;

TEST(backend_null_reports_failure)
{
	NullTranslatorBackend backend;
	TranslationResult received;
	bool called = false;
	backend.translate(TranslationRequest{"こんにちは", {}, "ja", "th"},
		[&](TranslationResult result) {
			received = std::move(result);
			called = true;
		});
	ASSERT_TRUE(called);
	ASSERT_FALSE(received.ok);
	ASSERT_FALSE(received.error.empty());
}

TEST(cache_returns_previous_translation)
{
	TranslationCache cache(4);
	ASSERT_FALSE(cache.get("こんにちは", "th").has_value());
	cache.put("こんにちは", "th", "สวัสดี");
	const auto value = cache.get("こんにちは", "th");
	ASSERT_TRUE(value.has_value());
	ASSERT_EQ(*value, "สวัสดี");
	ASSERT_EQ(cache.size(), static_cast<size_t>(1));
}

TEST(cache_normalizes_key_and_separates_target_languages)
{
	TranslationCache cache(4);
	cache.put("ＡＢＣ", "th", "ไทย");
	ASSERT_TRUE(cache.get("ABC", "th").has_value());
	ASSERT_FALSE(cache.get("ABC", "en").has_value());
}

TEST(cache_evicts_least_recently_used)
{
	TranslationCache cache(2);
	cache.put("one", "th", "1");
	cache.put("two", "th", "2");
	ASSERT_TRUE(cache.get("one", "th").has_value());
	cache.put("three", "th", "3");
	ASSERT_TRUE(cache.get("one", "th").has_value());
	ASSERT_FALSE(cache.get("two", "th").has_value());
	ASSERT_TRUE(cache.get("three", "th").has_value());
	ASSERT_EQ(cache.size(), static_cast<size_t>(2));
}

TEST(cache_clear_removes_everything)
{
	TranslationCache cache(4);
	cache.put("a", "th", "b");
	cache.clear();
	ASSERT_EQ(cache.size(), static_cast<size_t>(0));
	ASSERT_FALSE(cache.get("a", "th").has_value());
}
