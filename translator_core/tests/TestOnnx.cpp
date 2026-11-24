// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Integration tests that exercise the real ONNX Runtime backend and the full
// pipeline. Only built when TRANS_ENABLE_ONNXRUNTIME is on.
#include "TestHarness.hpp"

#include <trans/OcrEngine.hpp>
#include <trans/TextProcessor.hpp>
#include <trans/TranslatorPipeline.hpp>

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace trans;

namespace {

std::string Asset(const std::string &relative)
{
	return std::string(TRANS_ASSET_DIR) + "/" + relative;
}

std::string SourceFile(const std::string &relative)
{
	return std::string(TRANS_TEST_DIR) + "/" + relative;
}

bool ReadToken(std::istream &stream, std::string &token)
{
	token.clear();
	char character = 0;
	while (stream.get(character))
	{
		if (character == '#')
		{
			std::string ignored;
			std::getline(stream, ignored);
			continue;
		}
		if (!std::isspace(static_cast<unsigned char>(character)))
			break;
	}
	if (!stream)
		return false;
	token.push_back(character);
	while (stream.get(character) && !std::isspace(static_cast<unsigned char>(character)))
		token.push_back(character);
	return true;
}

Image ReadPpm(const std::string &path)
{
	Image image;
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return image;
	std::string magic, width, height, maximum;
	if (!ReadToken(stream, magic) || magic != "P6" || !ReadToken(stream, width) ||
			!ReadToken(stream, height) || !ReadToken(stream, maximum))
		return image;
	const int w = std::stoi(width);
	const int h = std::stoi(height);
	image.width = w;
	image.height = h;
	image.stride = w * 3;
	image.pixels.resize(static_cast<size_t>(w) * h * 3);
	stream.read(reinterpret_cast<char *>(image.pixels.data()),
		static_cast<std::streamsize>(image.pixels.size()));
	return image;
}

OcrConfig MakeOcrConfig()
{
	OcrConfig config;
	config.detection_model = Asset("models/ch_PP-OCRv3_det.onnx");
	config.recognition_model = Asset("models/japan_PP-OCRv3_rec.onnx");
	config.dictionary = Asset("models/japan_dict.txt");
	config.threads = 2;
	return config;
}

class EchoBackend final : public TranslatorBackend
{
public:
	void translate(const TranslationRequest &request, TranslationCallback callback) override
	{
		++calls;
		TranslationResult result;
		result.ok = true;
		result.text = "TH:" + request.text;
		callback(std::move(result));
	}
	const char *name() const override { return "echo"; }
	bool offline() const override { return true; }
	std::atomic<int> calls{0};
};

} // namespace

TEST(onnx_ocr_recognizes_real_subtitle_image)
{
	auto engine = createOnnxOcrEngine();
	ASSERT_TRUE(engine != nullptr);
	std::string error;
	ASSERT_TRUE(engine->load(MakeOcrConfig(), &error));
	ASSERT_TRUE(error.empty());

	const Image image = ReadPpm(SourceFile("tests/fixtures/subtitle.ppm"));
	ASSERT_FALSE(image.empty());

	const std::vector<OcrLine> lines = engine->recognize(image, &error);
	ASSERT_TRUE(error.empty());
	ASSERT_FALSE(lines.empty());
	const std::string text = joinOcrLines(lines);
	ASSERT_TRUE(text.find("ゲーム") != std::string::npos);
	ASSERT_TRUE(text.find("続けよう") != std::string::npos);
}

TEST(pipeline_with_real_ocr_produces_translation)
{
	std::shared_ptr<OcrEngine> ocr = createOnnxOcrEngine();
	ASSERT_TRUE(ocr != nullptr);
	auto backend = std::make_shared<EchoBackend>();

	PipelineConfig config;
	config.frame.roi = Rect{0, 0, 720, 80};
	config.frame.sample_interval_ms = 100;
	config.frame.stable_ms = 250;
	config.preprocess = OcrPreprocess::None;
	config.ocr = MakeOcrConfig();

	std::mutex mutex;
	std::condition_variable condition;
	std::vector<TranslationItem> items;
	PipelineCallbacks callbacks;
	callbacks.onTranslation = [&](const TranslationItem &item) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			items.push_back(item);
		}
		condition.notify_all();
	};
	callbacks.onError = [&](const std::string &error) {
		std::fprintf(stderr, "pipeline error: %s\n", error.c_str());
	};

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, std::move(callbacks), &error));
	ASSERT_TRUE(error.empty());

	const Image image = ReadPpm(SourceFile("tests/fixtures/subtitle.ppm"));
	pipeline.manualTrigger();
	pipeline.submitImage(image, 0);

	bool received = false;
	{
		std::unique_lock<std::mutex> lock(mutex);
		received = condition.wait_for(lock, std::chrono::seconds(5),
			[&]() { return !items.empty(); });
	}
	ASSERT_TRUE(received);
	ASSERT_TRUE(items[0].source_text.find("ゲーム") != std::string::npos);
	ASSERT_EQ(items[0].translated_text.rfind("TH:", 0), static_cast<size_t>(0));
	ASSERT_TRUE(pipeline.stats().ocr_runs >= 1);
	pipeline.stop();
}
