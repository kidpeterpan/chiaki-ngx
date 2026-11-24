// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/FrameProcessor.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace trans {
namespace {

inline int clampByte(float value)
{
	if (value <= 0.0f)
		return 0;
	if (value >= 255.0f)
		return 255;
	return static_cast<int>(value + 0.5f);
}

inline const uint8_t *planeRow(const FrameView &frame, int plane, int y)
{
	return frame.data[plane] + static_cast<size_t>(y) * static_cast<size_t>(frame.stride[plane]);
}

inline int lumaAt(const FrameView &frame, int x, int y)
{
	if (frame.format == PixelFormat::P010)
	{
		const uint8_t *row = planeRow(frame, 0, y);
		uint16_t sample = 0;
		std::memcpy(&sample, row + static_cast<size_t>(x) * 2, sizeof(sample));
		return sample >> 8;
	}
	return planeRow(frame, 0, y)[x];
}

inline void chromaAt(const FrameView &frame, int x, int y, int &cb, int &cr)
{
	const int cx = x / 2;
	const int cy = y / 2;
	const bool wide = frame.format == PixelFormat::P010;
	if (frame.format == PixelFormat::NV12 || frame.format == PixelFormat::P010)
	{
		const uint8_t *row = planeRow(frame, 1, cy);
		if (wide)
		{
			uint16_t pair[2] = {0, 0};
			std::memcpy(pair, row + static_cast<size_t>(cx) * 4, sizeof(pair));
			cb = pair[0] >> 8;
			cr = pair[1] >> 8;
		}
		else
		{
			cb = row[cx * 2];
			cr = row[cx * 2 + 1];
		}
		return;
	}

	const uint8_t *u_row = planeRow(frame, 1, cy);
	const uint8_t *v_row = planeRow(frame, 2, cy);
	cb = u_row[cx];
	cr = v_row[cx];
}

inline Rgb limitedYuvToRgb(int y, int cb, int cr, ColorMatrix matrix)
{
	const float luma = 1.164f * static_cast<float>(y - 16);
	const float d = static_cast<float>(cb - 128);
	const float e = static_cast<float>(cr - 128);
	float r;
	float g;
	float b;
	if (matrix == ColorMatrix::BT601Limited)
	{
		r = luma + 1.596f * e;
		g = luma - 0.392f * d - 0.813f * e;
		b = luma + 2.017f * d;
	}
	else
	{
		r = luma + 1.793f * e;
		g = luma - 0.213f * d - 0.533f * e;
		b = luma + 2.112f * d;
	}
	return Rgb{static_cast<uint8_t>(clampByte(r)), static_cast<uint8_t>(clampByte(g)),
			static_cast<uint8_t>(clampByte(b))};
}

void ensureSize(Image &image, int width, int height)
{
	if (image.width != width || image.height != height)
	{
		image.width = width;
		image.height = height;
		image.stride = width * 3;
		image.pixels.resize(static_cast<size_t>(width) * height * 3);
	}
}

void ensureSize(GrayscaleFrame &image, int width, int height)
{
	if (image.width != width || image.height != height)
	{
		image.width = width;
		image.height = height;
		image.stride = width;
		image.pixels.resize(static_cast<size_t>(width) * height);
	}
}

} // namespace

bool extractRgb(const FrameView &frame, const Rect &roi, Image &out, ColorMatrix matrix)
{
	if (!frame.valid() || roi.empty())
		return false;

	const Rect clipped = intersect(roi, Rect{0, 0, frame.width, frame.height});
	if (clipped.empty())
		return false;

	ensureSize(out, clipped.width, clipped.height);

	for (int y = 0; y < clipped.height; ++y)
	{
		uint8_t *dst = out.mutableRow(y);
		const int source_y = clipped.y + y;
		for (int x = 0; x < clipped.width; ++x)
		{
			const int source_x = clipped.x + x;
			Rgb rgb;
			switch (frame.format)
			{
			case PixelFormat::RGBA:
			{
				const uint8_t *src = planeRow(frame, 0, source_y) + static_cast<size_t>(source_x) * 4;
				rgb = Rgb{src[0], src[1], src[2]};
				break;
			}
			case PixelFormat::BGRA:
			{
				const uint8_t *src = planeRow(frame, 0, source_y) + static_cast<size_t>(source_x) * 4;
				rgb = Rgb{src[2], src[1], src[0]};
				break;
			}
			case PixelFormat::GRAY8:
			{
				const uint8_t value = planeRow(frame, 0, source_y)[source_x];
				rgb = Rgb{value, value, value};
				break;
			}
			default:
			{
				int cb = 128;
				int cr = 128;
				chromaAt(frame, source_x, source_y, cb, cr);
				rgb = limitedYuvToRgb(lumaAt(frame, source_x, source_y), cb, cr, matrix);
				break;
			}
			}
			dst[x * 3] = rgb.r;
			dst[x * 3 + 1] = rgb.g;
			dst[x * 3 + 2] = rgb.b;
		}
	}
	return true;
}

bool extractGray(const FrameView &frame, const Rect &roi, GrayscaleFrame &out, ColorMatrix matrix)
{
	Image rgb;
	if (!extractRgb(frame, roi, rgb, matrix))
		return false;

	ensureSize(out, rgb.width, rgb.height);
	for (int y = 0; y < rgb.height; ++y)
	{
		const uint8_t *src = rgb.row(y);
		uint8_t *dst = out.pixels.data() + static_cast<size_t>(y) * out.stride;
		for (int x = 0; x < rgb.width; ++x)
		{
			const int luma = (77 * src[x * 3] + 150 * src[x * 3 + 1] + 29 * src[x * 3 + 2]) >> 8;
			dst[x] = static_cast<uint8_t>(luma);
		}
	}
	return true;
}

Image grayToRgb(const GrayscaleFrame &frame)
{
	Image rgb;
	if (frame.empty())
		return rgb;
	ensureSize(rgb, frame.width, frame.height);
	for (int y = 0; y < frame.height; ++y)
	{
		const uint8_t *src = frame.pixels.data() + static_cast<size_t>(y) * frame.stride;
		uint8_t *dst = rgb.mutableRow(y);
		for (int x = 0; x < frame.width; ++x)
		{
			dst[x * 3] = src[x];
			dst[x * 3 + 1] = src[x];
			dst[x * 3 + 2] = src[x];
		}
	}
	return rgb;
}

DiffStats diffGray(const GrayscaleFrame &before, const GrayscaleFrame &after, int pixel_delta_threshold)
{
	DiffStats stats;
	if (before.width != after.width || before.height != after.height || before.empty())
	{
		stats.changed_ratio = 1.0;
		stats.mean_abs_delta = 255.0;
		stats.structure_similarity = 0.0;
		return stats;
	}

	const int threshold = std::max(1, pixel_delta_threshold);
	uint64_t changed = 0;
	uint64_t total_delta = 0;
	for (int y = 0; y < before.height; ++y)
	{
		const uint8_t *a = before.pixels.data() + static_cast<size_t>(y) * before.stride;
		const uint8_t *b = after.pixels.data() + static_cast<size_t>(y) * after.stride;
		for (int x = 0; x < before.width; ++x)
		{
			const int delta = std::abs(static_cast<int>(a[x]) - static_cast<int>(b[x]));
			total_delta += static_cast<uint64_t>(delta);
			if (delta >= threshold)
				++changed;
		}
	}

	const double pixels = static_cast<double>(before.width) * before.height;
	stats.changed_ratio = static_cast<double>(changed) / pixels;
	stats.mean_abs_delta = static_cast<double>(total_delta) / pixels;
	stats.structure_similarity = structureSimilarity(before, after);
	return stats;
}

double structureSimilarity(const GrayscaleFrame &a, const GrayscaleFrame &b)
{
	if (a.width != b.width || a.height != b.height || a.empty())
		return a.empty() && b.empty() ? 1.0 : 0.0;

	const double pixels = static_cast<double>(a.width) * a.height;
	double sum_a = 0.0;
	double sum_b = 0.0;
	double sum_aa = 0.0;
	double sum_bb = 0.0;
	double sum_ab = 0.0;
	for (int y = 0; y < a.height; ++y)
	{
		const uint8_t *row_a = a.pixels.data() + static_cast<size_t>(y) * a.stride;
		const uint8_t *row_b = b.pixels.data() + static_cast<size_t>(y) * b.stride;
		for (int x = 0; x < a.width; ++x)
		{
			const double va = row_a[x];
			const double vb = row_b[x];
			sum_a += va;
			sum_b += vb;
			sum_aa += va * va;
			sum_bb += vb * vb;
			sum_ab += va * vb;
		}
	}

	const double mean_a = sum_a / pixels;
	const double mean_b = sum_b / pixels;
	const double var_a = sum_aa / pixels - mean_a * mean_a;
	const double var_b = sum_bb / pixels - mean_b * mean_b;
	const double cov = sum_ab / pixels - mean_a * mean_b;
	constexpr double c1 = (0.01 * 255.0) * (0.01 * 255.0);
	constexpr double c2 = (0.03 * 255.0) * (0.03 * 255.0);

	const double numerator = (2.0 * mean_a * mean_b + c1) * (2.0 * cov + c2);
	const double denominator = (mean_a * mean_a + mean_b * mean_b + c1) * (var_a + var_b + c2);
	if (denominator <= 0.0)
		return 1.0;
	const double value = numerator / denominator;
	return std::clamp(value, 0.0, 1.0);
}

double standardDeviation(const GrayscaleFrame &frame)
{
	if (frame.empty())
		return 0.0;
	const double pixels = static_cast<double>(frame.width) * frame.height;
	double sum = 0.0;
	double sum_squares = 0.0;
	for (int y = 0; y < frame.height; ++y)
	{
		const uint8_t *row = frame.pixels.data() + static_cast<size_t>(y) * frame.stride;
		for (int x = 0; x < frame.width; ++x)
		{
			sum += row[x];
			sum_squares += static_cast<double>(row[x]) * row[x];
		}
	}
	const double mean = sum / pixels;
	const double variance = std::max(0.0, sum_squares / pixels - mean * mean);
	return std::sqrt(variance);
}

GrayscaleFrame resizeGray(const GrayscaleFrame &frame, int max_width)
{
	if (frame.empty() || max_width <= 0 || frame.width <= max_width)
		return frame;

	GrayscaleFrame resized;
	resized.width = max_width;
	resized.height = std::max(1, static_cast<int>(std::lround(
		static_cast<double>(frame.height) * max_width / frame.width)));
	resized.stride = resized.width;
	resized.pixels.resize(static_cast<size_t>(resized.width) * resized.height);

	for (int y = 0; y < resized.height; ++y)
	{
		const int y0 = static_cast<int>(static_cast<int64_t>(y) * frame.height / resized.height);
		const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<int64_t>(y + 1) * frame.height / resized.height));
		for (int x = 0; x < resized.width; ++x)
		{
			const int x0 = static_cast<int>(static_cast<int64_t>(x) * frame.width / resized.width);
			const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<int64_t>(x + 1) * frame.width / resized.width));
			uint32_t sum = 0;
			uint32_t count = 0;
			for (int sy = y0; sy < y1; ++sy)
			{
				const uint8_t *row = frame.pixels.data() + static_cast<size_t>(sy) * frame.stride;
				for (int sx = x0; sx < x1; ++sx)
				{
					sum += row[sx];
					++count;
				}
			}
			resized.pixels[static_cast<size_t>(y) * resized.stride + x] =
				static_cast<uint8_t>(count ? sum / count : 0);
		}
	}
	return resized;
}

bool enhanceContrast(GrayscaleFrame &frame, double low_percentile, double high_percentile)
{
	if (frame.empty())
		return false;

	uint64_t histogram[256] = {0};
	for (uint8_t value : frame.pixels)
		++histogram[value];

	const uint64_t total = frame.pixels.size();
	const uint64_t low_rank = static_cast<uint64_t>(total * std::clamp(low_percentile, 0.0, 100.0) / 100.0);
	const uint64_t high_rank = static_cast<uint64_t>(total * std::clamp(high_percentile, 0.0, 100.0) / 100.0);

	int low = 0;
	int high = 255;
	uint64_t cumulative = 0;
	for (int value = 0; value < 256; ++value)
	{
		cumulative += histogram[value];
		if (cumulative > low_rank)
		{
			low = value;
			break;
		}
	}
	cumulative = 0;
	for (int value = 0; value < 256; ++value)
	{
		cumulative += histogram[value];
		if (cumulative >= high_rank)
		{
			high = value;
			break;
		}
	}

	if (high <= low + 1)
		return false;

	const double scale = 255.0 / static_cast<double>(high - low);
	for (uint8_t &value : frame.pixels)
	{
		const double stretched = (static_cast<double>(value) - low) * scale;
		value = static_cast<uint8_t>(std::clamp(stretched, 0.0, 255.0));
	}
	return true;
}

bool adaptiveThreshold(const GrayscaleFrame &frame, GrayscaleFrame &out, int block_size, int constant)
{
	if (frame.empty())
		return false;

	int block = block_size;
	if (block < 3)
		block = 3;
	if (block % 2 == 0)
		++block;
	if (standardDeviation(frame) < 1.0)
		return false;

	const int width = frame.width;
	const int height = frame.height;
	std::vector<uint64_t> integral(static_cast<size_t>(width + 1) * (height + 1), 0);
	for (int y = 0; y < height; ++y)
	{
		uint64_t row_sum = 0;
		const uint8_t *row = frame.pixels.data() + static_cast<size_t>(y) * frame.stride;
		for (int x = 0; x < width; ++x)
		{
			row_sum += row[x];
			integral[static_cast<size_t>(y + 1) * (width + 1) + (x + 1)] =
				integral[static_cast<size_t>(y) * (width + 1) + (x + 1)] + row_sum;
		}
	}

	ensureSize(out, width, height);
	const int radius = block / 2;
	for (int y = 0; y < height; ++y)
	{
		uint8_t *dst = out.pixels.data() + static_cast<size_t>(y) * out.stride;
		const int y0 = std::max(0, y - radius);
		const int y1 = std::min(height - 1, y + radius);
		for (int x = 0; x < width; ++x)
		{
			const int x0 = std::max(0, x - radius);
			const int x1 = std::min(width - 1, x + radius);
			const uint64_t sum = integral[static_cast<size_t>(y1 + 1) * (width + 1) + (x1 + 1)] -
				integral[static_cast<size_t>(y0) * (width + 1) + (x1 + 1)] -
				integral[static_cast<size_t>(y1 + 1) * (width + 1) + x0] +
				integral[static_cast<size_t>(y0) * (width + 1) + x0];
			const uint64_t count = static_cast<uint64_t>(y1 - y0 + 1) * (x1 - x0 + 1);
			const int mean = static_cast<int>(sum / count);
			dst[x] = frame.at(x, y) >= mean - constant ? 255 : 0;
		}
	}
	return true;
}

FrameProcessor::FrameProcessor(FrameProcessorConfig config) : config_(config) {}

void FrameProcessor::reset()
{
	last_gray_.clear();
	last_sample_ms_ = -1;
	still_ms_ = 0;
	generation_ = 0;
	emitted_generation_ = 0;
	initialized_ = false;
}

ProcessResult FrameProcessor::process(const FrameView &frame, int64_t now_ms, bool force)
{
	ProcessResult result;
	if (!frame.valid())
		return result;
	if (!force && last_sample_ms_ >= 0 && now_ms - last_sample_ms_ < config_.sample_interval_ms)
		return result;

	const int64_t delta_ms = last_sample_ms_ < 0 ? 0 : std::max<int64_t>(0, now_ms - last_sample_ms_);
	last_sample_ms_ = now_ms;

	GrayscaleFrame gray;
	if (!extractGray(frame, config_.roi, gray))
		return result;
	return evaluate(gray, delta_ms);
}

ProcessResult FrameProcessor::processGray(const GrayscaleFrame &gray, int64_t now_ms, bool force)
{
	ProcessResult result;
	if (gray.empty())
		return result;
	if (!force && last_sample_ms_ >= 0 && now_ms - last_sample_ms_ < config_.sample_interval_ms)
		return result;

	const int64_t delta_ms = last_sample_ms_ < 0 ? 0 : std::max<int64_t>(0, now_ms - last_sample_ms_);
	last_sample_ms_ = now_ms;
	return evaluate(gray, delta_ms);
}

ProcessResult FrameProcessor::evaluate(const GrayscaleFrame &gray, int64_t delta_ms)
{
	ProcessResult result;
	result.sampled = true;
	result.stddev = standardDeviation(gray);
	result.generation = generation_;

	GrayscaleFrame diff_frame = config_.diff_max_width > 0 && gray.width > config_.diff_max_width
		? resizeGray(gray, config_.diff_max_width)
		: gray;

	if (!initialized_)
	{
		initialized_ = true;
		generation_ = 1;
		result.generation = generation_;
		result.changed_ratio = 1.0;
		last_gray_ = std::move(diff_frame);
		return result;
	}

	const DiffStats stats = diffGray(last_gray_, diff_frame, config_.pixel_delta_threshold);
	result.changed_ratio = stats.changed_ratio;
	result.mean_abs_delta = stats.mean_abs_delta;
	result.structure_similarity = stats.structure_similarity;

	const bool content_changed = stats.changed_ratio > config_.still_changed_ratio ||
		stats.mean_abs_delta > config_.still_mean_abs_delta;
	const bool has_content = result.stddev >= config_.min_stddev;

	if (content_changed || !has_content)
	{
		if (content_changed)
		{
			++generation_;
			emitted_generation_ = 0;
		}
		still_ms_ = 0;
	}
	else
	{
		still_ms_ += delta_ms;
		if (still_ms_ >= config_.stable_ms && emitted_generation_ != generation_ && generation_ != 0)
		{
			emitted_generation_ = generation_;
			result.stable = true;
			result.gray = gray;
		}
	}

	result.generation = generation_;
	result.still_ms = still_ms_;
	last_gray_ = std::move(diff_frame);
	return result;
}

} // namespace trans
