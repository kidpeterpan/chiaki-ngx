// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// ROI extraction, change detection, and the text-stabilization state machine.
#pragma once

#include <trans/Types.hpp>

#include <cstdint>

namespace trans {

enum class ColorMatrix
{
	BT709Limited,
	BT601Limited,
};

struct DiffStats
{
	double changed_ratio = 0.0;        // fraction of pixels whose luma moved by >= threshold
	double mean_abs_delta = 0.0;       // mean |a-b| over every pixel
	double structure_similarity = 1.0; // global luma structure similarity, 0..1
};

// Converts an ROI of a decoded frame to packed RGB888. The rect is clamped to
// the frame; returns false when nothing remains.
bool extractRgb(const FrameView &frame, const Rect &roi, Image &out,
		ColorMatrix matrix = ColorMatrix::BT709Limited);

// Same as extractRgb, but returns a single-channel luma image.
bool extractGray(const FrameView &frame, const Rect &roi, GrayscaleFrame &out,
		ColorMatrix matrix = ColorMatrix::BT709Limited);

DiffStats diffGray(const GrayscaleFrame &before, const GrayscaleFrame &after,
		int pixel_delta_threshold = 14);

// Global (non-windowed) luminance structure similarity. Cheap enough for a
// subtitle ROI at 10 Hz; not a replacement for windowed SSIM.
double structureSimilarity(const GrayscaleFrame &a, const GrayscaleFrame &b);

double standardDeviation(const GrayscaleFrame &frame);

GrayscaleFrame resizeGray(const GrayscaleFrame &frame, int max_width);

// Percentile contrast stretch. Returns false when the range is already flat.
bool enhanceContrast(GrayscaleFrame &frame, double low_percentile = 1.0,
		double high_percentile = 99.0);

// Adaptive threshold via integral image. Writes 0 or 255 into out.
bool adaptiveThreshold(const GrayscaleFrame &frame, GrayscaleFrame &out,
		int block_size = 15, int constant = 8);

Image grayToRgb(const GrayscaleFrame &frame);

struct FrameProcessorConfig
{
	Rect roi;
	int sample_interval_ms = 100;      // hard cap of 10 Hz by default
	int stable_ms = 250;               // quiet time before OCR is triggered
	double still_changed_ratio = 0.002; // <= 0.2% of pixels may change
	double still_mean_abs_delta = 1.0;  // streaming noise floor
	int pixel_delta_threshold = 14;
	double min_stddev = 2.0;           // reject blank / transparent frames
	int diff_max_width = 640;          // downscale only the diff, never the crop
	// Stillness is judged on text-like pixels (the extreme-brightness minority), so a scene
	// moving behind a see-through dialogue box does not keep resetting it.
	double text_flip_ratio = 0.02;     // share of text pixels that may flip between samples
	int text_bright_threshold = 200;   // light text on a dark area
	int text_dark_threshold = 55;      // dark text on a light area
	// Until the whole area has kept changing this long, any whole-area change still counts as a
	// change (a new line in grey or coloured text on a still screen); after it, only text pixels.
	int moving_area_after_ms = 500;
};

struct ProcessResult
{
	bool sampled = false;
	bool stable = false;
	uint64_t generation = 0;
	double changed_ratio = 0.0;
	double mean_abs_delta = 0.0;
	double structure_similarity = 1.0;
	double stddev = 0.0;
	int64_t still_ms = 0;
	double text_flip_ratio = 0.0;
	int64_t unsettled_ms = 0;          // how long the whole area has kept changing
	GrayscaleFrame gray;
};

class FrameProcessor
{
public:
	explicit FrameProcessor(FrameProcessorConfig config = {});

	// now_ms must be monotonic. force bypasses the sampling interval and is
	// used by the manual "translate now" key.
	ProcessResult process(const FrameView &frame, int64_t now_ms, bool force = false);

	// Same stabilization logic for an already-extracted ROI. The pipeline uses
	// this after the render thread has copied the ROI out of the decoded frame.
	ProcessResult processGray(const GrayscaleFrame &gray, int64_t now_ms, bool force = false);

	void reset();
	void setRoi(const Rect &roi) { config_.roi = roi; }
	const Rect &roi() const { return config_.roi; }
	const FrameProcessorConfig &config() const { return config_; }

private:
	FrameProcessorConfig config_;
	GrayscaleFrame last_gray_;
	std::vector<uint8_t> last_text_mask_;
	int64_t last_sample_ms_ = -1;
	int64_t still_ms_ = 0;
	int64_t unsettled_ms_ = 0;
	uint64_t generation_ = 0;
	uint64_t emitted_generation_ = 0;
	bool initialized_ = false;

	ProcessResult evaluate(const GrayscaleFrame &gray, int64_t delta_ms);
};

} // namespace trans
