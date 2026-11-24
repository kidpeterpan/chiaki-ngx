// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Manual verification for the offline NLLB backend.
//
//   nllb_probe <model_dir> <sentencepiece.model> <source_lang> <target_lang> <text>
#include <trans/NllbBackend.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>

int main(int argc, char **argv)
{
	if (argc < 6)
	{
		std::fprintf(stderr, "usage: %s <model_dir> <spm> <src> <tgt> <text>\n", argv[0]);
		return 2;
	}
	trans::NllbConfig config;
	config.model_dir = argv[1];
	config.sentencepiece_model = argv[2];
	config.source_language = argv[3];
	config.target_language = argv[4];
	std::string error;
	const auto load_begin = std::chrono::steady_clock::now();
	auto backend = trans::createNllbBackend(config, &error);
	if (!backend)
	{
		std::fprintf(stderr, "load failed: %s\n", error.c_str());
		return 1;
	}
	std::fprintf(stderr, "load_ms=%.0f\n",
		std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - load_begin).count());

	std::mutex mutex;
	std::condition_variable condition;
	trans::TranslationResult received;
	bool done = false;
	trans::TranslationRequest request;
	request.text = argv[5];
	request.source_language = argv[3];
	request.target_language = argv[4];
	backend->translate(request, [&](trans::TranslationResult result) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			received = std::move(result);
			done = true;
		}
		condition.notify_all();
	});
	{
		std::unique_lock<std::mutex> lock(mutex);
		condition.wait_for(lock, std::chrono::seconds(60), [&]() { return done; });
	}
	if (!received.ok)
	{
		std::fprintf(stderr, "translate failed: %s\n", received.error.c_str());
		return 1;
	}
	std::printf("%s\n", received.text.c_str());
	std::fprintf(stderr, "translation_ms=%lld\n", static_cast<long long>(received.latency_ms));
	return 0;
}
