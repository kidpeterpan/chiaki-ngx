// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/TranslatorPipeline.hpp>

#include <trans/RingBuffer.hpp>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace trans {
namespace {

int64_t NowMilliseconds()
{
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

GrayscaleFrame ImageToGray(const Image &image)
{
	GrayscaleFrame gray;
	if (image.empty())
		return gray;
	gray.width = image.width;
	gray.height = image.height;
	gray.stride = image.width;
	gray.pixels.resize(static_cast<size_t>(image.width) * image.height);
	for (int y = 0; y < image.height; ++y)
	{
		const uint8_t *source = image.row(y);
		uint8_t *destination = gray.pixels.data() + static_cast<size_t>(y) * gray.stride;
		for (int x = 0; x < image.width; ++x)
		{
			destination[x] = static_cast<uint8_t>(
				(77 * source[x * 3] + 150 * source[x * 3 + 1] + 29 * source[x * 3 + 2]) >> 8);
		}
	}
	return gray;
}

void ApplyThreadAffinity(const std::vector<int> &cores)
{
#if defined(__linux__)
	if (cores.empty())
		return;
	cpu_set_t set;
	CPU_ZERO(&set);
	for (int core : cores)
	{
		if (core >= 0 && core < CPU_SETSIZE)
			CPU_SET(core, &set);
	}
	pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
#else
	(void)cores;
#endif
}

} // namespace

struct PipelineShared
{
	explicit PipelineShared(const PipelineConfig &configuration)
		: config(configuration), processor(configuration.frame), roi(configuration.frame.roi),
		  context(configuration.context_pairs),
		  dedup(DedupConfig{0.9, 16, configuration.dedup_ttl_ms}),
		  cache(configuration.cache_capacity)
	{
	}

	PipelineConfig config;
	FrameProcessor processor;
	std::shared_ptr<OcrEngine> ocr;
	std::shared_ptr<TranslatorBackend> backend;
	PipelineCallbacks callbacks;

	SpscRingBuffer<TranslatorPipeline::QueuedFrame, 4> queue;

	std::atomic<bool> running{false};
	std::mutex wait_mutex;
	std::condition_variable wait_condition;

	std::mutex config_mutex;
	Rect roi;
	bool roi_pending = false;

	std::mutex state_mutex;
	ConversationContext context;
	std::deque<TranslationItem> backlog;

	TextDedupCache dedup;
	TranslationCache cache;

	std::atomic<uint64_t> frames_submitted{0};
	std::atomic<uint64_t> frames_dropped{0};
	std::atomic<uint64_t> samples{0};
	std::atomic<uint64_t> ocr_runs{0};
	std::atomic<uint64_t> translations{0};
	std::atomic<uint64_t> duplicates{0};
	std::atomic<uint64_t> cache_hits{0};
	std::atomic<uint64_t> errors{0};
	std::atomic<double> last_ocr_ms{0.0};
	std::atomic<double> last_translation_ms{0.0};
	std::atomic<uint64_t> next_id{1};
};

TranslatorPipeline::TranslatorPipeline() = default;

TranslatorPipeline::~TranslatorPipeline()
{
	stop();
}

bool TranslatorPipeline::start(const PipelineConfig &config, std::shared_ptr<OcrEngine> ocr,
		std::shared_ptr<TranslatorBackend> backend, PipelineCallbacks callbacks, std::string *error)
{
	if (running())
	{
		if (error)
			*error = "pipeline is already running";
		return false;
	}
	if (!ocr || !backend)
	{
		if (error)
			*error = "pipeline requires both an OCR engine and a translation backend";
		return false;
	}
	if (config.frame.roi.empty())
	{
		if (error)
			*error = "pipeline ROI is not configured";
		return false;
	}

	if (!ocr->ready())
	{
		std::string load_error;
		if (!ocr->load(config.ocr, &load_error))
		{
			if (error)
				*error = load_error.empty() ? "OCR engine failed to load" : load_error;
			return false;
		}
	}

	auto shared = std::make_shared<PipelineShared>(config);
	shared->ocr = std::move(ocr);
	shared->backend = std::move(backend);
	shared->callbacks = std::move(callbacks);
	shared->roi = config.frame.roi;
	shared->running.store(true);
	shared_ = shared;
	worker_ = std::thread(&TranslatorPipeline::run, this, shared);
	if (error)
		error->clear();
	return true;
}

void TranslatorPipeline::stop()
{
	if (shared_)
	{
		shared_->running.store(false);
	}
	if (worker_.joinable())
		worker_.join();
}

bool TranslatorPipeline::running() const
{
	return shared_ && shared_->running.load();
}

void TranslatorPipeline::run(const std::shared_ptr<PipelineShared> &shared)
{
	ApplyThreadAffinity(shared->config.worker_cpu_affinity);
	while (shared->running.load())
	{
		QueuedFrame frame;
		if (!shared->queue.tryPop(frame))
		{
			std::unique_lock<std::mutex> lock(shared->wait_mutex);
			shared->wait_condition.wait_for(lock, std::chrono::milliseconds(20), [&shared]() {
				return !shared->running.load() || shared->queue.size() > 0;
			});
			continue;
		}
		handleFrame(shared, std::move(frame));
	}
}

void TranslatorPipeline::handleFrame(const std::shared_ptr<PipelineShared> &shared, QueuedFrame frame)
{
	if (!shared->running.load() || frame.image.empty())
		return;

	{
		std::lock_guard<std::mutex> lock(shared->config_mutex);
		if (shared->roi_pending)
		{
			shared->processor.setRoi(shared->roi);
			shared->roi_pending = false;
		}
	}

	const GrayscaleFrame gray = ImageToGray(frame.image);
	if (gray.empty())
		return;

	const ProcessResult result = shared->processor.processGray(gray, frame.now_ms, true);
	if (!result.sampled)
		return;
	shared->samples.fetch_add(1);

	const bool manual = frame.manual;
	if (shared->config.manual_only && !manual)
		return;
	if (!result.stable && !manual)
		return;
	if (result.stddev < shared->config.frame.min_stddev)
		return;

	shared->ocr_runs.fetch_add(1);

	GrayscaleFrame prepared = gray;
	switch (shared->config.preprocess)
	{
	case OcrPreprocess::Contrast:
		enhanceContrast(prepared);
		break;
	case OcrPreprocess::Threshold:
	{
		GrayscaleFrame binary;
		if (adaptiveThreshold(prepared, binary))
		{
			for (uint8_t &value : binary.pixels)
				value = static_cast<uint8_t>(255 - value);
			prepared = std::move(binary);
		}
		break;
	}
	case OcrPreprocess::None:
		break;
	}

	const int64_t ocr_begin = NowMilliseconds();
	std::string ocr_error;
	std::vector<OcrLine> lines;
	try
	{
		lines = shared->ocr->recognize(grayToRgb(prepared), &ocr_error);
	}
	catch (const std::exception &exception)
	{
		ocr_error = exception.what();
	}
	shared->last_ocr_ms.store(static_cast<double>(NowMilliseconds() - ocr_begin));

	if (!ocr_error.empty())
	{
		fail(shared, ocr_error);
		return;
	}
	if (lines.empty())
	{
		if (shared->callbacks.onState)
			shared->callbacks.onState("no-text");
		return;
	}

	float confidence = 0.0f;
	for (const OcrLine &line : lines)
		confidence += line.confidence;
	confidence /= static_cast<float>(lines.size());
	const std::string text = joinOcrLines(lines);
	if (!isTranslatableText(text))
	{
		if (shared->callbacks.onState)
			shared->callbacks.onState("noise");
		return;
	}

	if (shared->callbacks.onOcr)
		shared->callbacks.onOcr(text, confidence);

	if (!manual && shared->dedup.isDuplicate(text, frame.now_ms))
	{
		shared->duplicates.fetch_add(1);
		if (shared->callbacks.onState)
			shared->callbacks.onState("duplicate");
		return;
	}
	shared->dedup.remember(text, frame.now_ms);

	const std::string &target_language = shared->config.prompt.target_language;
	if (const std::optional<std::string> cached = shared->cache.get(text, target_language))
	{
		shared->cache_hits.fetch_add(1);
		deliver(shared, text, confidence, frame.now_ms, true, *cached);
		return;
	}

	TranslationRequest request;
	request.text = text;
	{
		std::lock_guard<std::mutex> lock(shared->state_mutex);
		request.context = shared->context.pairs();
	}
	request.source_language = shared->config.prompt.source_language;
	request.target_language = target_language;

	const int64_t source_timestamp = frame.now_ms;
	try
	{
		shared->backend->translate(request, [shared, text, confidence, source_timestamp](
			TranslationResult translation) {
			if (!translation.ok)
			{
				fail(shared, translation.error.empty() ? "translation failed" : translation.error);
				return;
			}
			shared->last_translation_ms.store(static_cast<double>(translation.latency_ms));
			shared->cache.put(text, shared->config.prompt.target_language, translation.text);
			deliver(shared, text, confidence, source_timestamp, false, translation.text);
		});
	}
	catch (const std::exception &exception)
	{
		fail(shared, exception.what());
	}
}

void TranslatorPipeline::deliver(std::shared_ptr<PipelineShared> shared, const std::string &source,
		float confidence, int64_t source_timestamp_ms, bool from_cache,
		const std::string &translation)
{
	if (!shared->running.load())
		return;

	TranslationItem item;
	item.id = shared->next_id.fetch_add(1);
	item.source_text = source;
	item.translated_text = translation;
	item.ocr_confidence = confidence;
	item.source_timestamp_ms = source_timestamp_ms;
	item.translated_timestamp_ms = NowMilliseconds();
	item.from_cache = from_cache;

	{
		std::lock_guard<std::mutex> lock(shared->state_mutex);
		shared->context.add(source, translation);
		shared->backlog.push_back(item);
		while (shared->backlog.size() > shared->config.backlog_limit)
			shared->backlog.pop_front();
	}
	shared->translations.fetch_add(1);
	if (shared->callbacks.onTranslation)
		shared->callbacks.onTranslation(item);
}

void TranslatorPipeline::fail(const std::shared_ptr<PipelineShared> &shared, const std::string &error)
{
	shared->errors.fetch_add(1);
	if (shared->callbacks.onError)
		shared->callbacks.onError(error);
}

void TranslatorPipeline::submitFrame(const FrameView &frame, int64_t now_ms)
{
	const std::shared_ptr<PipelineShared> shared = shared_;
	if (!shared || !shared->running.load())
		return;

	Rect roi;
	{
		std::lock_guard<std::mutex> lock(shared->config_mutex);
		roi = shared->roi;
	}
	if (roi.empty())
		return;

	const bool manual = manual_requested_.exchange(false);
	const int64_t last = last_submit_ms_.load();
	if (!manual && last >= 0 && now_ms - last < shared->config.frame.sample_interval_ms)
		return;

	Image image;
	if (!extractRgb(frame, roi, image))
	{
		shared->frames_dropped.fetch_add(1);
		if (manual)
			manual_requested_.store(true);
		return;
	}
	last_submit_ms_.store(now_ms);

	QueuedFrame queued;
	queued.image = std::move(image);
	queued.now_ms = now_ms;
	queued.manual = manual;
	if (!shared->queue.tryPush(std::move(queued)))
	{
		shared->frames_dropped.fetch_add(1);
		if (manual)
			manual_requested_.store(true);
		return;
	}
	shared->frames_submitted.fetch_add(1);
}

void TranslatorPipeline::submitImage(const Image &image, int64_t now_ms)
{
	const std::shared_ptr<PipelineShared> shared = shared_;
	if (!shared || !shared->running.load() || image.empty())
		return;

	const bool manual = manual_requested_.exchange(false);
	const int64_t last = last_submit_ms_.load();
	if (!manual && last >= 0 && now_ms - last < shared->config.frame.sample_interval_ms)
		return;
	last_submit_ms_.store(now_ms);

	QueuedFrame queued;
	queued.image = image;
	queued.now_ms = now_ms;
	queued.manual = manual;
	if (!shared->queue.tryPush(std::move(queued)))
	{
		shared->frames_dropped.fetch_add(1);
		if (manual)
			manual_requested_.store(true);
		return;
	}
	shared->frames_submitted.fetch_add(1);
}

void TranslatorPipeline::manualTrigger()
{
	manual_requested_.store(true);
}

void TranslatorPipeline::setRoi(const Rect &roi)
{
	const std::shared_ptr<PipelineShared> shared = shared_;
	if (!shared)
		return;
	std::lock_guard<std::mutex> lock(shared->config_mutex);
	shared->roi = roi;
	shared->roi_pending = true;
}

std::vector<TranslationItem> TranslatorPipeline::backlog() const
{
	const std::shared_ptr<PipelineShared> shared = shared_;
	if (!shared)
		return {};
	std::lock_guard<std::mutex> lock(shared->state_mutex);
	return std::vector<TranslationItem>(shared->backlog.begin(), shared->backlog.end());
}

PipelineStats TranslatorPipeline::stats() const
{
	PipelineStats snapshot;
	const std::shared_ptr<PipelineShared> shared = shared_;
	if (!shared)
		return snapshot;
	snapshot.frames_submitted = shared->frames_submitted.load();
	snapshot.frames_dropped = shared->frames_dropped.load();
	snapshot.samples = shared->samples.load();
	snapshot.ocr_runs = shared->ocr_runs.load();
	snapshot.translations = shared->translations.load();
	snapshot.duplicates = shared->duplicates.load();
	snapshot.cache_hits = shared->cache_hits.load();
	snapshot.errors = shared->errors.load();
	snapshot.last_ocr_ms = shared->last_ocr_ms.load();
	snapshot.last_translation_ms = shared->last_translation_ms.load();
	return snapshot;
}

} // namespace trans
