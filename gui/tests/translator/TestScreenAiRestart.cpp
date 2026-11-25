// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Chrome Screen AI across pipeline restarts, with the real libchromescreenai.so (Linux x86_64,
// fetched by scripts/translator/fetch-screenai.sh). The library aborts the process when it is
// initialized twice, which a settings change used to do; every step here must read the text.
//
//   translator_screenai_restart_tests <screenai resources dir> <fixture.ppm>
#include "translator/screenai.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>

namespace {

const char *const kExpectedText = "ゲームを続けよう";

trans::Image ReadPpm(const std::string &path)
{
	trans::Image image;
	std::ifstream in(path, std::ios::binary);
	std::string magic;
	int max_value = 0;
	in >> magic >> image.width >> image.height >> max_value;
	in.get();
	image.stride = image.width * 3;
	image.pixels.resize(static_cast<size_t>(image.stride) * image.height);
	in.read(reinterpret_cast<char *>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
	return image;
}

std::unique_ptr<ScreenAiEngine> Load(const std::string &models, const char *step)
{
	auto engine = std::make_unique<ScreenAiEngine>();
	trans::OcrConfig config;
	config.screenai_model_dir = models;
	std::string error;
	if (!engine->load(config, &error))
	{
		std::fprintf(stderr, "FAIL: %s: load failed: %s\n", step, error.c_str());
		return nullptr;
	}
	return engine;
}

bool Reads(ScreenAiEngine &engine, const trans::Image &image, const char *step)
{
	std::string error;
	const auto lines = engine.recognize(image, &error);
	if (lines.empty() || lines.front().text != kExpectedText)
	{
		std::fprintf(stderr, "FAIL: %s: expected '%s', got %zu line(s) %s\n", step, kExpectedText,
			lines.size(), error.c_str());
		return false;
	}
	return true;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		std::fprintf(stderr, "usage: %s <screenai resources dir> <fixture.ppm>\n", argv[0]);
		return 2;
	}
	const std::string models = argv[1];
	const trans::Image image = ReadPpm(argv[2]);

	// A settings change restarts the pipeline: TranslatorManager builds and loads the new
	// engine before the old pipeline state, which owns the old engine, is released.
	auto current = Load(models, "first load");
	if (!current || !Reads(*current, image, "first load"))
		return 1;
	for (int round = 1; round <= 5; ++round)
	{
		auto next = Load(models, "restart");
		if (!next || !Reads(*next, image, "restart, both engines alive"))
			return 1;
		current = std::move(next);
		if (!Reads(*current, image, "restart, previous engine destroyed"))
			return 1;
	}
	std::printf("PASS five restarts\n");

	// Switching OCR away destroys every engine; switching back loads it again.
	current.reset();
	auto again = Load(models, "switch back");
	if (!again || !Reads(*again, image, "switch back"))
		return 1;
	std::printf("PASS switch away and back\n");
	return 0;
}
