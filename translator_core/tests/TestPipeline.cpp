// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/TranslatorPipeline.hpp>

#include <algorithm>
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
		const int call = ++calls;
		if (error)
			error->clear();
		if (!script.empty())
			return script[static_cast<size_t>(std::min<int>(call, static_cast<int>(script.size())) - 1)];
		if (!multiple_lines.empty())
			return multiple_lines;
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
	std::vector<OcrLine> multiple_lines;
	// One result per call; the last one repeats.
	std::vector<std::vector<OcrLine>> script;
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

// A scene that never settles: every pixel, bright and dark, moves between submissions.
Image MakeMovingImage(int width, int height, int step)
{
	Image image;
	image.width = width;
	image.height = height;
	image.stride = width * 3;
	image.pixels.resize(static_cast<size_t>(width) * height * 3);
	for (int y = 0; y < height; ++y)
	{
		uint8_t *row = image.mutableRow(y);
		for (int x = 0; x < width; ++x)
		{
			const uint8_t value = static_cast<uint8_t>((x * 31 + y * 17 + step * 53) % 256);
			row[x * 3] = value;
			row[x * 3 + 1] = value;
			row[x * 3 + 2] = value;
		}
	}
	return image;
}

// Submits a moving image every 100 ms of stream time from `from_ms` up to (not including)
// `to_ms`, waiting for the worker to sample each one so no frame is dropped.
void FeedMoving(TranslatorPipeline &pipeline, int64_t from_ms, int64_t to_ms)
{
	for (int64_t now = from_ms; now < to_ms; now += 100)
	{
		const uint64_t before = pipeline.stats().samples;
		pipeline.submitImage(MakeMovingImage(64, 32, static_cast<int>(now / 100)), now);
		for (int wait = 0; wait < 500 && pipeline.stats().samples == before; ++wait)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

OcrLine Line(const std::string &text, int x)
{
	OcrLine line;
	line.text = text;
	line.confidence = 0.9f;
	line.box = Rect{x, 10, 100, 30};
	return line;
}

// Submits the same still image every 100 ms from `from_ms` up to `to_ms`.
void FeedStill(TranslatorPipeline &pipeline, const Image &image, int64_t from_ms, int64_t to_ms)
{
	for (int64_t now = from_ms; now < to_ms; now += 100)
	{
		const uint64_t before = pipeline.stats().samples;
		pipeline.submitImage(image, now);
		for (int wait = 0; wait < 500 && pipeline.stats().samples == before; ++wait)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

bool HasState(Collector &collector, const std::string &state)
{
	std::lock_guard<std::mutex> lock(collector.mutex);
	return std::find(collector.states.begin(), collector.states.end(), state) != collector.states.end();
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

TEST(pipeline_delivers_multiple_boxes_for_side_by_side_text)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	OcrLine left;
	left.text = "はい";
	left.confidence = 0.9f;
	left.box = Rect{10, 10, 100, 30};
	OcrLine right;
	right.text = "いいえ";
	right.confidence = 0.8f;
	right.box = Rect{300, 10, 100, 30};
	ocr->multiple_lines = {left, right};

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	pipeline.manualTrigger();
	pipeline.submitImage(MakeImage(500, 100, 200, 3), 0);
	ASSERT_TRUE(collector->waitForItems(2));
	ASSERT_EQ(backend->calls.load(), 2);
	ASSERT_EQ(collector->items[0].source_box.x, 10);
	ASSERT_EQ(collector->items[1].source_box.x, 300);
	ASSERT_EQ(collector->items[0].source_width, 500);
	ASSERT_EQ(collector->items[0].batch_id, collector->items[1].batch_id);
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

TEST(pipeline_fallback_translates_a_line_after_two_matching_reads)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 4000);

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(1));
	ASSERT_EQ(collector->items[0].source_text, "こんにちは");
	const PipelineStats stats = pipeline.stats();
	ASSERT_TRUE(stats.ocr_runs >= 2 && stats.ocr_runs <= 4);
	ASSERT_TRUE(HasState(*collector, "unsettled"));
	ASSERT_TRUE(HasState(*collector, "fallback"));
	pipeline.stop();
}

TEST(pipeline_live_off_runs_no_automatic_ocr)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.manual_only = true;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 4000);
	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(0));
	ASSERT_EQ(pipeline.stats().ocr_runs, static_cast<uint64_t>(0));

	pipeline.manualTrigger();
	pipeline.submitImage(MakeImage(64, 32, 200, 3), 5000);
	ASSERT_TRUE(collector->waitForItems(1));
	pipeline.stop();
}

TEST(pipeline_set_manual_only_takes_effect_while_running)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	pipeline.setManualOnly(true);
	FeedMoving(pipeline, 0, 4000);
	const Image still = MakeImage(64, 32, 200, 3);
	for (int64_t now = 4000; now < 4500; now += 100)
		pipeline.submitImage(still, now);
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(0));
	ASSERT_EQ(pipeline.stats().ocr_runs, static_cast<uint64_t>(0));
	pipeline.stop();
}

TEST(pipeline_fallback_backs_off_after_empty_reads)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	ocr->text.clear();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 9000);

	// Reads at 1500, 3000 and 4500 find nothing; the next would be due at 9500, not 6000.
	ASSERT_EQ(pipeline.stats().ocr_runs, static_cast<uint64_t>(3));
	pipeline.stop();
}

TEST(pipeline_fallback_off_for_cloud_ocr)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.live_fallback = false;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 4000);

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(0));
	ASSERT_EQ(pipeline.stats().ocr_runs, static_cast<uint64_t>(0));
	pipeline.stop();
}

TEST(pipeline_fallback_does_not_reshow_a_line_past_the_ttl)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.dedup_ttl_ms = 1000;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 8000);

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(1));
	ASSERT_TRUE(pipeline.stats().ocr_runs >= 4);
	pipeline.stop();
}

// A name plate is confirmed and delivered on the same read that first sees the finished line;
// the line must still be delivered once the next read confirms it.
TEST(pipeline_fallback_delivers_a_block_first_seen_on_a_delivering_read)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	const std::string name = "アリス";
	const std::string full = "今日はとてもいい天気ですね、散歩に行きましょう";
	ocr->script = {
		{Line(name, 10), Line("今日は", 300)},
		{Line(name, 10), Line(full, 300)},
		{Line(name, 10), Line(full, 300)},
	};
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 6000);

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(2));
	ASSERT_EQ(collector->items[1].source_text, full);
	pipeline.stop();
}

// Reads that find only non-dialogue text (scene noise) back off like empty reads.
TEST(pipeline_fallback_backs_off_on_noise_reads)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	ocr->text = "1 2 3";
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 9000);

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(0));
	ASSERT_EQ(pipeline.stats().ocr_runs, static_cast<uint64_t>(3));
	pipeline.stop();
}

// Once its line is on screen, the fallback slows down instead of re-reading it every 1.5 s.
TEST(pipeline_fallback_backs_off_once_the_line_is_shown)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(MakeConfig(), ocr, backend, MakeCallbacks(collector), &error));
	FeedMoving(pipeline, 0, 15000);

	// 1500 read, 3000 deliver, 4500/6000/7500 already shown, then every 5 s: 12500.
	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(1));
	ASSERT_EQ(pipeline.stats().ocr_runs, static_cast<uint64_t>(6));
	pipeline.stop();
}

// A line read again by the stable path after the duplicate TTL (something crossed the text)
// is not shown again.
TEST(pipeline_stable_path_does_not_reshow_a_line_past_the_ttl)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.dedup_ttl_ms = 1000;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));
	const Image line = MakeImage(64, 32, 200, 3);
	FeedStill(pipeline, line, 0, 500);
	ASSERT_TRUE(collector->waitForItems(1));
	FeedStill(pipeline, MakeImage(64, 32, 200, 4), 2000, 2100);
	FeedStill(pipeline, line, 2100, 2600);

	ASSERT_TRUE(pipeline.stats().ocr_runs >= 2);
	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(1));
	pipeline.stop();
}

// After the box closes (a read with no text), the same line coming back is shown again.
TEST(pipeline_line_is_shown_again_after_the_box_closes)
{
	auto ocr = std::make_shared<FakeOcrEngine>();
	auto backend = std::make_shared<FakeBackend>();
	auto collector = std::make_shared<Collector>();

	PipelineConfig config = MakeConfig();
	config.dedup_ttl_ms = 1000;
	TranslatorPipeline pipeline;
	std::string error;
	ASSERT_TRUE(pipeline.start(config, ocr, backend, MakeCallbacks(collector), &error));
	const Image line = MakeImage(64, 32, 200, 3);
	FeedStill(pipeline, line, 0, 500);
	ASSERT_TRUE(collector->waitForItems(1));
	ocr->text.clear();
	FeedStill(pipeline, MakeImage(64, 32, 200, 6), 1000, 1500);
	ocr->text = "こんにちは";
	FeedStill(pipeline, line, 3000, 3500);

	ASSERT_EQ(collector->itemCount(), static_cast<size_t>(2));
	pipeline.stop();
}
