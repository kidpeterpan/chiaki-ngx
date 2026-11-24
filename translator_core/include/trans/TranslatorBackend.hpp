// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Translation backend contract shared by cloud APIs (implemented in the Qt
// GUI adapter) and offline engines (CTranslate2 / NLLB).
#pragma once

#include <trans/TextProcessor.hpp>

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trans {

struct TranslationRequest
{
	std::string text;
	std::vector<ContextPair> context;
	std::string source_language = "ja";
	std::string target_language = "th";
};

struct TranslationResult
{
	bool ok = false;
	std::string text;
	std::string error;
	int64_t latency_ms = 0;
};

using TranslationCallback = std::function<void(TranslationResult)>;

class TranslatorBackend
{
public:
	virtual ~TranslatorBackend() = default;
	virtual void translate(const TranslationRequest &request, TranslationCallback callback) = 0;
	virtual const char *name() const = 0;
	virtual bool offline() const { return false; }
};

class NullTranslatorBackend final : public TranslatorBackend
{
public:
	void translate(const TranslationRequest &, TranslationCallback callback) override;
	const char *name() const override { return "null"; }
};

// Small thread-safe LRU cache. Keys are normalized source text plus target
// language so the same line can be cached for Thai and English separately.
class TranslationCache
{
public:
	explicit TranslationCache(size_t capacity = 256);

	std::optional<std::string> get(std::string_view text, std::string_view target_language) const;
	void put(std::string_view text, std::string_view target_language, std::string translation);
	void clear();
	size_t size() const;

private:
	struct Entry
	{
		std::string value;
		std::list<std::string>::iterator position;
	};

	static std::string makeKey(std::string_view text, std::string_view target_language);

	size_t capacity_;
	mutable std::mutex mutex_;
	mutable std::list<std::string> order_;
	mutable std::unordered_map<std::string, Entry> entries_;
};

} // namespace trans
