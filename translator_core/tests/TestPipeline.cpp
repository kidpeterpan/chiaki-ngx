// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/TranslatorPipeline.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace trans;

namespace {

Image MakeImage(int width, int height, uint8_t base, int glyphs)
{
	Image image;
	image.width = width;
	image.height = height;
	image.stride = width * 3;
	image.pixels.assign(static_cast<size_t>(width) * height * 3, base);
	for (int y = 8; y < height - 8; ++y)
	{
		for (int glyph = 0; glyph < glyphs; ++glyph)
		{
			const int left = 10 + glyph * 12;
			for (int x = left; x < left + 7 && x < width - 4; ++x)
			{
				uint8_t *pixel = image.mutableRow(y) + x * 3;
				pixel[0] = 15;
				pixel[1] = 15;
				pixel[2] = 15;
			}
		}
	}
	return image;
}

class FakeOcrEngine final : public OcrEngine
{
public:
	bool load(const OcrConfig &, std::string *error) override
	{
		if (error)
			error->clear();
		return true;
	}

	std::vector<OcrLine> recognize(const Image &image, std::string *error) override
	{
		++calls;
		if (error)
			error->clear();
		if (text.empty())
			return {};
		OcrLine line;
		line.text = text;
		line.translated_text = translated;
		line.confidence = confidence;
		line.box = Rect{0, 0, image.width, image.height};
		return {line};
	}

	const char *name() const override { return "fake"; }
	bool ready() const override { return true; }

	std::string text = "こんにちは";
	std::string translated;
	float confidence = 0.94f;
	std::atomic<int> calls{0};
};

class FakeBackend final : public TranslatorBackend
{
public:
	void translate(const TranslationRequest &request, TranslationCallback callback) override
	{
		++calls;
		{
			std::lock_guard<std::mutex> lock(mutex);
			requests.push_back(request);
		}
		TranslationResult result;
		result.ok = true;
		result.text = "TH:" + request.text;
		callback(std::move(result));
	}

	const char *name() const override { return "fake"; }
	bool offline() const override { return true; }

	std::atomic<int> calls{0};
	std::mutex mutex;
	std::vector<TranslationRequest> requests;
};

struct Collector
{
	std::mutex mutex;
	std::condition_variable condition;
	std::vector<TranslationItem> items;
	std::vector<std::string> ocr;
	std::vector<std::string> errors;
	std::vector<std::string> states;

	void addItem(const TranslationItem &item)
	{
		std::lock_guard<std::mutex> lock(mutex);
		items.push_back(item);
		condition.notify_all();
	}

	bool waitForItems(size_t count, int timeout_ms = 3000)
	{
		std::unique_lock<std::mutex> lock(mutex);
		return condition.wait_for(lock, std::chrono::milliseconds(timeout_ms),
			[&]() { return items.size() >= count; });
	}

	size_t itemCount()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return items.size();
	}
};

PipelineCallbacks MakeCallbacks(std::shared_ptr<Collector> collector)
{
	PipelineCallbacks callbacks;
	callbacks.onTranslation = [collector](const TranslationItem &item) { collector->addItem(item); };
	callbacks.onOcr = [collector](const std::string &text, float) {
		std::lock_guard<std::mutex> lock(collector->mutex);
		collector->ocr.push_back(text);
	};
	callbacks.onError = [collector](const std::string &error) {
		std::lock_guard<std::mutex> lock(collector->mutex);
		collector->errors.push_back(error);
	};
	callbacks.onState = [collector](const std::string &state) {
		std::lock_guard<std::mutex> lock(collector->mutex);
		collector->states.push_back(state);
	};
	return callbacks;
}

PipelineConfig MakeConfig()
{
	PipelineConfig config;
	config.frame.roi = Rect{0, 0, 64, 32};
	config.frame.sample_interval_ms = 100;
	config.frame.stable_ms = 250;
	config.frame.still_changed_ratio = 0.002;
	config.preprocess = OcrPreprocess::None;
	return config;
}

} // namespace

TEST(pipeline_translates_after_text_stabilizes)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	ASSERT_TRUE(error.empty());

	const Image image = MakeImage(64, 32, 200, 3);
	for (int step = 0; step < 4; ++step)
		pipeline.submitImage(image, step * 100);

	ASSERT_TRUE(collector->waitForItems(1));
	ASSERT_EQ(collector->items[0].source_text, "こんにちは");
	ASSERT_EQ(collector->items[0].translated_text, "TH:こんにちは");
	ASSERT_TRUE(collector->items[0].ocr_confidence > 0.9f);

	const PipelineStats stats = pipeline.stats();
	ASSERT_TRUE(stats.ocr_runs >= 1);
	ASSERT_EQ(stats.translations, static_cast<uint64_t>(1));
	pipeline.stop();
}

TEST(pipeline_suppresses_near_duplicate_subtitles)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));

	const Image first = MakeImage(64, 32, 200, 2);
	for (int step = 0; step < 4; ++step)
		pipeline.submitImage(first, step * 100);
	ASSERT_TRUE(collector->waitForItems(1));

	ocr->text = "こんにちは！";
	const Image second = MakeImage(64, 32, 120, 5);
	pipeline.submitImage(second, 1000);
	pipeline.submitImage(second, 1100);
	pipeline.submitImage(second, 1200);
	pipeline.submitImage(second, 1300);
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	ASSERT_EQ(backend->calls.load(), 1);
	ASSERT_TRUE(pipeline.stats().duplicates >= 1);
	pipeline.stop();
}

TEST(pipeline_manual_trigger_skips_the_stability_wait)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));

	const Image image = MakeImage(64, 32, 200, 3);
	pipeline.manualTrigger();
	pipeline.submitImage(image, 0);

	ASSERT_TRUE(collector->waitForItems(1));
	ASSERT_EQ(collector->items[0].source_text, "こんにちは");
	pipeline.stop();
}

TEST(pipeline_uses_translation_from_a_vision_ocr_engine)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	ocr->text = "ゲームを続けよう";
	ocr->translated = "เล่นเกมต่อ";
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));

	pipeline.manualTrigger();
	pipeline.submitImage(MakeImage(64, 32, 200, 3), 0);
	ASSERT_TRUE(collector->waitForItems(1));
	ASSERT_EQ(collector->items[0].translated_text, "เล่นเกมต่อ");
	ASSERT_EQ(backend->calls.load(), 0);
	pipeline.stop();
}

TEST(pipeline_manual_trigger_uses_translation_cache)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));

	const Image image = MakeImage(64, 32, 200, 3);
	pipeline.manualTrigger();
	pipeline.submitImage(image, 0);
	ASSERT_TRUE(collector->waitForItems(1));

	pipeline.manualTrigger();
	pipeline.submitImage(image, 500);
	ASSERT_TRUE(collector->waitForItems(2));

	ASSERT_EQ(backend->calls.load(), 1);
	ASSERT_TRUE(collector->items[1].from_cache);
	ASSERT_TRUE(pipeline.stats().cache_hits >= 1);
	pipeline.stop();
}

TEST(pipeline_passes_three_recent_dialogue_pairs)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.context_pairs = 3;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));

	const Image image = MakeImage(64, 32, 200, 3);
	const char *texts[] = {"いち", "に", "さん", "よん"};
	for (int index = 0; index < 4; ++index)
	{
		ocr->text = texts[index];
		pipeline.manualTrigger();
		pipeline.submitImage(image, index * 500);
		ASSERT_TRUE(collector->waitForItems(static_cast<size_t>(index + 1)));
	}

	ASSERT_EQ(backend->requests.size(), static_cast<size_t>(4));
	const TranslationRequest &last = backend->requests[3];
	ASSERT_EQ(last.text, "よん");
	ASSERT_EQ(last.context.size(), static_cast<size_t>(3));
	ASSERT_EQ(last.context[0].source, "いち");
	ASSERT_EQ(last.context[1].source, "に");
	ASSERT_EQ(last.context[2].source, "さん");
	pipeline.stop();
}

TEST(pipeline_trims_backlog_to_configured_limit)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.backlog_limit = 3;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));

	const Image image = MakeImage(64, 32, 200, 3);
	for (int index = 0; index < 5; ++index)
	{
		ocr->text = "line" + std::to_string(index);
		pipeline.manualTrigger();
		pipeline.submitImage(image, index * 300);
		ASSERT_TRUE(collector->waitForItems(static_cast<size_t>(index + 1)));
	}

	const std::vector<TranslationItem> backlog = pipeline.backlog();
	ASSERT_EQ(backlog.size(), static_cast<size_t>(3));
	ASSERT_EQ(backlog.back().source_text, "line4");
	pipeline.stop();
}

TEST(pipeline_rejects_null_ocr_engine)
{
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_FALSE(pipeline.start(MakeConfig(), nullptr, backend, MakeCallbacks(collector), &error));
	ASSERT_FALSE(error.empty());
	ASSERT_FALSE(pipeline.running());
}

TEST(pipeline_does_not_translate_without_change_or_manual_trigger)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));

	const Image image = MakeImage(64, 32, 200, 3);
	pipeline.submitImage(image, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(350));

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(0));
	ASSERT_EQ(backend->calls.load(), 0);
	pipeline.stop();
}
