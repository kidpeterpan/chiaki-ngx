// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Owns the OCR + translation worker thread. The render thread only copies the
// configured ROI into the queue; everything expensive happens off-thread.
#pragma once

#include <trans/FrameProcessor.hpp>
#include <trans/OcrEngine.hpp>
#include <trans/TextProcessor.hpp>
#include <trans/TranslatorBackend.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace trans {

enum class OcrPreprocess
{
	None,
	Contrast,
	Threshold,
};

struct PipelineConfig
{
	FrameProcessorConfig frame;
	OcrConfig ocr;
	PromptConfig prompt;
	OcrPreprocess preprocess = OcrPreprocess::Contrast;
	bool manual_only = false;
	size_t context_pairs = 3;
	size_t backlog_limit = 100;
	size_t cache_capacity = 256;
	int64_t dedup_ttl_ms = 30000;
	std::vector<int> worker_cpu_affinity;
};

struct PipelineStats
{
	uint64_t frames_submitted = 0;
	uint64_t frames_dropped = 0;
	uint64_t samples = 0;
	uint64_t ocr_runs = 0;
	uint64_t translations = 0;
	uint64_t duplicates = 0;
	uint64_t cache_hits = 0;
	uint64_t errors = 0;
	double last_ocr_ms = 0.0;
	double last_translation_ms = 0.0;
};

struct PipelineCallbacks
{
	std::function<void(const std::string &source, float confidence)> onOcr;
	std::function<void(const TranslationItem &item)> onTranslation;
	std::function<void(const std::string &error)> onError;
	std::function<void(const std::string &state)> onState;
};

struct PipelineShared;

class TranslatorPipeline
{
public:
	TranslatorPipeline();
	~TranslatorPipeline();
	TranslatorPipeline(const TranslatorPipeline &) = delete;
	TranslatorPipeline &operator=(const TranslatorPipeline &) = delete;

	bool start(const PipelineConfig &config, std::shared_ptr<OcrEngine> ocr,
			std::shared_ptr<TranslatorBackend> backend, PipelineCallbacks callbacks,
			std::string *error);
	void stop();
	bool running() const;

	// Render-thread entry points. submitFrame copies only the ROI and returns.
	void submitFrame(const FrameView &frame, int64_t now_ms);
	void submitImage(const Image &image, int64_t now_ms);
	void manualTrigger();

	void setRoi(const Rect &roi);
	std::vector<TranslationItem> backlog() const;
	PipelineStats stats() const;

private:
	friend struct PipelineShared;

	struct QueuedFrame
	{
		Image image;
		int64_t now_ms = 0;
		bool manual = false;
	};

	void run(const std::shared_ptr<PipelineShared> &shared);
	void handleFrame(const std::shared_ptr<PipelineShared> &shared, QueuedFrame frame);
	static void deliver(std::shared_ptr<PipelineShared> shared, const std::string &source,
			float confidence, int64_t source_timestamp_ms, bool from_cache,
			const std::string &translation, const Rect &source_box = {},
			int source_width = 0, int source_height = 0, uint64_t batch_id = 0);
	static void fail(const std::shared_ptr<PipelineShared> &shared, const std::string &error);

	std::shared_ptr<PipelineShared> shared_;
	std::thread worker_;
	std::atomic<bool> manual_requested_{false};
	std::atomic<int64_t> last_submit_ms_{-1};
};

} // namespace trans
