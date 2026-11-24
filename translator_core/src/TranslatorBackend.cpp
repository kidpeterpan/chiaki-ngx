// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/TranslatorBackend.hpp>

#include <utility>

namespace trans {

void NullTranslatorBackend::translate(const TranslationRequest &, TranslationCallback callback)
{
	TranslationResult result;
	result.ok = false;
	result.error = "no translation backend configured";
	if (callback)
		callback(std::move(result));
}

TranslationCache::TranslationCache(size_t capacity) : capacity_(capacity) {}

std::string TranslationCache::makeKey(std::string_view text, std::string_view target_language)
{
	return normalizeText(text) + '\n' + std::string(target_language);
}

std::optional<std::string> TranslationCache::get(std::string_view text,
		std::string_view target_language) const
{
	const std::string key = makeKey(text, target_language);
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = entries_.find(key);
	if (found == entries_.end())
		return std::nullopt;
	order_.splice(order_.begin(), order_, found->second.position);
	return found->second.value;
}

void TranslationCache::put(std::string_view text, std::string_view target_language,
		std::string translation)
{
	if (capacity_ == 0)
		return;
	const std::string key = makeKey(text, target_language);
	std::lock_guard<std::mutex> lock(mutex_);
	const auto found = entries_.find(key);
	if (found != entries_.end())
	{
		found->second.value = std::move(translation);
		order_.splice(order_.begin(), order_, found->second.position);
		return;
	}

	while (entries_.size() >= capacity_)
	{
		const std::string stale = order_.back();
		order_.pop_back();
		entries_.erase(stale);
	}
	order_.push_front(key);
	entries_.emplace(key, Entry{std::move(translation), order_.begin()});
}

void TranslationCache::clear()
{
	std::lock_guard<std::mutex> lock(mutex_);
	entries_.clear();
	order_.clear();
}

size_t TranslationCache::size() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return entries_.size();
}

} // namespace trans
