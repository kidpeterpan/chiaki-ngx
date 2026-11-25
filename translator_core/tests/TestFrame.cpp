// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/FrameProcessor.hpp>
#include <trans/RingBuffer.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

using namespace trans;

namespace {

GrayscaleFrame MakeGray(int width, int height, uint8_t fill)
{
	GrayscaleFrame frame;
	frame.width = width;
	frame.height = height;
	frame.stride = width;
	frame.pixels.assign(static_cast<size_t>(width) * height, fill);
	return frame;
}

// A plausible subtitle frame: light text box with dark glyph blocks.
void DrawGlyphs(GrayscaleFrame &frame, int glyph_count)
{
	for (int y = 8; y < frame.height - 8; ++y)
	{
		for (int x = 6; x < frame.width - 6; ++x)
			frame.pixels[static_cast<size_t>(y) * frame.stride + x] = 200;
	}
	for (int glyph = 0; glyph < glyph_count; ++glyph)
	{
		const int left = 12 + glyph * 10;
		for (int y = 12; y < frame.height - 12; ++y)
		{
			for (int x = left; x < left + 6 && x < frame.width - 6; ++x)
				frame.pixels[static_cast<size_t>(y) * frame.stride + x] = 20;
		}
	}
}

// A scene seen through a see-through dialogue box: a narrow brightness band around `base`
// whose pattern shifts on every sample.
void FillMovingBackground(GrayscaleFrame &frame, int sample, int base, int amplitude)
{
	const int span = 2 * amplitude + 1;
	for (int y = 0; y < frame.height; ++y)
	{
		for (int x = 0; x < frame.width; ++x)
		{
			const int value = base - amplitude + (x * 7 + y * 3 + sample * 13) % span;
			frame.pixels[static_cast<size_t>(y) * frame.stride + x] = static_cast<uint8_t>(value);
		}
	}
}

// Glyph blocks at positions that depend on `variant`, so two variants are two different lines.
void DrawGlyphLine(GrayscaleFrame &frame, int variant, uint8_t value)
{
	for (int glyph = 0; glyph < 6; ++glyph)
	{
		const int left = 10 + glyph * 16 + (variant % 2) * 8;
		const int top = variant == 0 ? 10 : 18;
		for (int y = top; y < top + 12; ++y)
		{
			for (int x = left; x < left + 6; ++x)
				frame.pixels[static_cast<size_t>(y) * frame.stride + x] = value;
		}
	}
}

// Glyph `index` of a dialogue box laid out in rows of 22, 28 px cells with 45% ink.
void DrawGlyphAt(GrayscaleFrame &frame, int index, uint8_t value)
{
	const int left = 10 + (index % 22) * 28;
	const int top = 10 + (index / 22) * 40;
	for (int y = top + 4; y < top + 26; ++y)
	{
		for (int x = left + 4; x < left + 18; ++x)
			frame.pixels[static_cast<size_t>(y) * frame.stride + x] = value;
	}
}

void DrawBorder(GrayscaleFrame &frame, uint8_t value)
{
	for (int x = 0; x < frame.width; ++x)
	{
		for (int y : {0, 1, frame.height - 2, frame.height - 1})
			frame.pixels[static_cast<size_t>(y) * frame.stride + x] = value;
	}
}

FrameProcessorConfig LiveConfig(int width, int height)
{
	FrameProcessorConfig config;
	config.roi = Rect{0, 0, width, height};
	config.sample_interval_ms = 100;
	config.stable_ms = 250;
	config.still_changed_ratio = 0.002;
	config.still_mean_abs_delta = 1.0;
	config.pixel_delta_threshold = 14;
	config.min_stddev = 2.0;
	return config;
}

FrameView GrayView(const GrayscaleFrame &frame)
{
	FrameView view;
	view.data[0] = frame.pixels.data();
	view.stride[0] = frame.stride;
	view.width = frame.width;
	view.height = frame.height;
	view.format = PixelFormat::GRAY8;
	return view;
}

} // namespace

TEST(frame_extracts_nv12_limited_white)
{
	const int width = 32;
	const int height = 18;
	std::vector<uint8_t> luma(static_cast<size_t>(width) * height, 235);
	std::vector<uint8_t> chroma(static_cast<size_t>(width / 2) * (height / 2) * 2, 128);

	FrameView view;
	view.data[0] = luma.data();
	view.stride[0] = width;
	view.data[1] = chroma.data();
	view.stride[1] = width;
	view.width = width;
	view.height = height;
	view.format = PixelFormat::NV12;

	Image rgb;
	ASSERT_TRUE(extractRgb(view, Rect{0, 0, width, height}, rgb));
	ASSERT_EQ(rgb.width, width);
	ASSERT_EQ(rgb.height, height);
	const Rgb pixel = rgb.pixel(4, 4);
	ASSERT_NEAR(pixel.r, 255, 2);
	ASSERT_NEAR(pixel.g, 255, 2);
	ASSERT_NEAR(pixel.b, 255, 2);
}

TEST(frame_extracts_nv12_limited_black)
{
	const int width = 16;
	const int height = 8;
	std::vector<uint8_t> luma(static_cast<size_t>(width) * height, 16);
	std::vector<uint8_t> chroma(static_cast<size_t>(width / 2) * (height / 2) * 2, 128);

	FrameView view;
	view.data[0] = luma.data();
	view.stride[0] = width;
	view.data[1] = chroma.data();
	view.stride[1] = width;
	view.width = width;
	view.height = height;
	view.format = PixelFormat::NV12;

	Image rgb;
	ASSERT_TRUE(extractRgb(view, Rect{0, 0, width, height}, rgb));
	const Rgb pixel = rgb.pixel(0, 0);
	ASSERT_NEAR(pixel.r, 0, 2);
	ASSERT_NEAR(pixel.g, 0, 2);
	ASSERT_NEAR(pixel.b, 0, 2);
}

TEST(frame_nv12_and_i420_produce_same_pixels)
{
	const int width = 16;
	const int height = 8;
	std::vector<uint8_t> luma(static_cast<size_t>(width) * height);
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
			luma[static_cast<size_t>(y) * width + x] = static_cast<uint8_t>(40 + x * 8 + y * 4);
	}
	std::vector<uint8_t> u(static_cast<size_t>(width / 2) * (height / 2), 90);
	std::vector<uint8_t> v(static_cast<size_t>(width / 2) * (height / 2), 170);
	std::vector<uint8_t> interleaved;
	for (size_t i = 0; i < u.size(); ++i)
	{
		interleaved.push_back(u[i]);
		interleaved.push_back(v[i]);
	}

	FrameView nv12;
	nv12.data[0] = luma.data();
	nv12.stride[0] = width;
	nv12.data[1] = interleaved.data();
	nv12.stride[1] = width;
	nv12.width = width;
	nv12.height = height;
	nv12.format = PixelFormat::NV12;

	FrameView i420;
	i420.data[0] = luma.data();
	i420.stride[0] = width;
	i420.data[1] = u.data();
	i420.stride[1] = width / 2;
	i420.data[2] = v.data();
	i420.stride[2] = width / 2;
	i420.width = width;
	i420.height = height;
	i420.format = PixelFormat::I420;

	Image nv12_rgb;
	Image i420_rgb;
	ASSERT_TRUE(extractRgb(nv12, Rect{0, 0, width, height}, nv12_rgb));
	ASSERT_TRUE(extractRgb(i420, Rect{0, 0, width, height}, i420_rgb));
	ASSERT_TRUE(nv12_rgb.pixels == i420_rgb.pixels);
}

TEST(frame_extracts_p010_like_nv12)
{
	const int width = 8;
	const int height = 4;
	std::vector<uint16_t> luma(static_cast<size_t>(width) * height, 235 << 8);
	std::vector<uint16_t> chroma(static_cast<size_t>(width / 2) * (height / 2) * 2, 128 << 8);

	FrameView view;
	view.data[0] = reinterpret_cast<const uint8_t *>(luma.data());
	view.stride[0] = width * 2;
	view.data[1] = reinterpret_cast<const uint8_t *>(chroma.data());
	view.stride[1] = width * 2;
	view.width = width;
	view.height = height;
	view.format = PixelFormat::P010;

	Image rgb;
	ASSERT_TRUE(extractRgb(view, Rect{0, 0, width, height}, rgb));
	ASSERT_NEAR(rgb.pixel(0, 0).r, 255, 2);
	ASSERT_NEAR(rgb.pixel(0, 0).g, 255, 2);
}

TEST(frame_extracts_rgba_passthrough)
{
	const int width = 4;
	const int height = 2;
	std::vector<uint8_t> rgba;
	for (int i = 0; i < width * height; ++i)
	{
		rgba.push_back(10);
		rgba.push_back(20);
		rgba.push_back(30);
		rgba.push_back(255);
	}

	FrameView view;
	view.data[0] = rgba.data();
	view.stride[0] = width * 4;
	view.width = width;
	view.height = height;
	view.format = PixelFormat::RGBA;

	Image rgb;
	ASSERT_TRUE(extractRgb(view, Rect{0, 0, width, height}, rgb));
	const Rgb pixel = rgb.pixel(1, 1);
	ASSERT_EQ(static_cast<int>(pixel.r), 10);
	ASSERT_EQ(static_cast<int>(pixel.g), 20);
	ASSERT_EQ(static_cast<int>(pixel.b), 30);
}

TEST(frame_roi_is_clamped_to_frame_bounds)
{
	const int width = 16;
	const int height = 8;
	std::vector<uint8_t> luma(static_cast<size_t>(width) * height, 128);
	std::vector<uint8_t> chroma(static_cast<size_t>(width / 2) * (height / 2) * 2, 128);

	FrameView view;
	view.data[0] = luma.data();
	view.stride[0] = width;
	view.data[1] = chroma.data();
	view.stride[1] = width;
	view.width = width;
	view.height = height;
	view.format = PixelFormat::NV12;

	Image rgb;
	ASSERT_TRUE(extractRgb(view, Rect{-5, -5, 10, 10}, rgb));
	ASSERT_EQ(rgb.width, 5);
	ASSERT_EQ(rgb.height, 5);

	// A rect entirely outside the frame is an error, not an empty image.
	ASSERT_FALSE(extractRgb(view, Rect{100, 100, 10, 10}, rgb));
}

TEST(frame_diff_reports_changed_pixels)
{
	GrayscaleFrame before = MakeGray(32, 16, 128);
	GrayscaleFrame after = before;
	for (int y = 0; y < 8; ++y)
	{
		for (int x = 0; x < 8; ++x)
			after.pixels[static_cast<size_t>(y) * after.stride + x] = 180;
	}

	const DiffStats stats = diffGray(before, after, 14);
	ASSERT_NEAR(stats.changed_ratio, 64.0 / 512.0, 0.0001);
	ASSERT_NEAR(stats.mean_abs_delta, (52.0 * 64.0) / 512.0, 0.01);
	ASSERT_TRUE(stats.structure_similarity < 1.0);
}

TEST(frame_diff_identical_frames_is_zero)
{
	const GrayscaleFrame frame = MakeGray(16, 16, 90);
	const DiffStats stats = diffGray(frame, frame, 10);
	ASSERT_NEAR(stats.changed_ratio, 0.0, 1e-9);
	ASSERT_NEAR(stats.mean_abs_delta, 0.0, 1e-9);
	ASSERT_NEAR(stats.structure_similarity, 1.0, 1e-9);
}

TEST(frame_structure_similarity_detects_layout_change)
{
	GrayscaleFrame checker = MakeGray(32, 32, 0);
	for (int y = 0; y < 32; ++y)
		for (int x = 0; x < 32; ++x)
			checker.pixels[static_cast<size_t>(y) * checker.stride + x] =
				((x / 4 + y / 4) % 2) ? 230 : 20;

	GrayscaleFrame inverted = checker;
	for (uint8_t &value : inverted.pixels)
		value = static_cast<uint8_t>(255 - value);

	ASSERT_TRUE(structureSimilarity(checker, checker) > 0.999);
	ASSERT_TRUE(structureSimilarity(checker, inverted) < 0.01);
}

TEST(frame_stabilizer_emits_once_after_quiet_period)
{
	FrameProcessorConfig config;
	config.roi = Rect{0, 0, 64, 32};
	config.sample_interval_ms = 100;
	config.stable_ms = 250;
	config.still_changed_ratio = 0.002;
	config.pixel_delta_threshold = 14;
	config.min_stddev = 2.0;

	FrameProcessor processor(config);
	GrayscaleFrame frame = MakeGray(64, 32, 200);
	DrawGlyphs(frame, 3);

	ASSERT_FALSE(processor.process(GrayView(frame), 0).stable);
	ASSERT_FALSE(processor.process(GrayView(frame), 100).stable);
	ASSERT_FALSE(processor.process(GrayView(frame), 200).stable);
	const ProcessResult stable = processor.process(GrayView(frame), 300);
	ASSERT_TRUE(stable.stable);

	// The same text must not be submitted to OCR again for every sample.
	ASSERT_FALSE(processor.process(GrayView(frame), 400).stable);
	ASSERT_FALSE(processor.process(GrayView(frame), 500).stable);
}

TEST(frame_stabilizer_never_fires_while_text_is_typing)
{
	FrameProcessorConfig config;
	config.roi = Rect{0, 0, 64, 32};
	config.sample_interval_ms = 100;
	config.stable_ms = 250;
	config.still_changed_ratio = 0.002;

	FrameProcessor processor(config);
	bool emitted = false;
	for (int glyphs = 1; glyphs <= 5; ++glyphs)
	{
		GrayscaleFrame frame = MakeGray(64, 32, 200);
		DrawGlyphs(frame, glyphs);
		emitted = emitted || processor.process(GrayView(frame), glyphs * 100).stable;
	}
	ASSERT_FALSE(emitted);
}

TEST(frame_stabilizer_ignores_blank_frames)
{
	FrameProcessorConfig config;
	config.roi = Rect{0, 0, 64, 32};
	config.sample_interval_ms = 100;
	config.stable_ms = 250;
	config.min_stddev = 2.0;

	FrameProcessor processor(config);
	const GrayscaleFrame blank = MakeGray(64, 32, 128);
	ASSERT_FALSE(processor.process(GrayView(blank), 0).stable);
	ASSERT_FALSE(processor.process(GrayView(blank), 100).stable);
	ASSERT_FALSE(processor.process(GrayView(blank), 200).stable);
	ASSERT_FALSE(processor.process(GrayView(blank), 300).stable);
}

TEST(frame_text_settles_over_moving_background)
{
	FrameProcessor processor(LiveConfig(120, 40));
	int stable_samples = 0;
	ProcessResult last;
	for (int sample = 0; sample < 10; ++sample)
	{
		GrayscaleFrame frame = MakeGray(120, 40, 0);
		FillMovingBackground(frame, sample, 60, 30);
		DrawGlyphLine(frame, 0, 240);
		last = processor.process(GrayView(frame), sample * 100);
		if (sample > 0)
			ASSERT_TRUE(last.changed_ratio > 0.002);
		if (last.stable)
			++stable_samples;
	}
	ASSERT_EQ(stable_samples, 1);
	ASSERT_TRUE(last.unsettled_ms > 0);
}

TEST(frame_text_change_starts_new_generation)
{
	FrameProcessor processor(LiveConfig(120, 40));
	std::vector<uint64_t> stable_generations;
	for (int sample = 0; sample < 16; ++sample)
	{
		GrayscaleFrame frame = MakeGray(120, 40, 0);
		FillMovingBackground(frame, sample, 60, 30);
		DrawGlyphLine(frame, sample < 10 ? 0 : 1, 240);
		const ProcessResult result = processor.process(GrayView(frame), sample * 100);
		if (result.stable)
			stable_generations.push_back(result.generation);
	}
	ASSERT_EQ(stable_generations.size(), static_cast<size_t>(2));
	ASSERT_TRUE(stable_generations[0] != stable_generations[1]);
}

TEST(frame_dark_text_on_light_box_settles)
{
	FrameProcessor processor(LiveConfig(120, 40));
	bool emitted = false;
	for (int sample = 0; sample < 10; ++sample)
	{
		GrayscaleFrame frame = MakeGray(120, 40, 0);
		FillMovingBackground(frame, sample, 230, 20);
		DrawGlyphLine(frame, 0, 20);
		emitted = processor.process(GrayView(frame), sample * 100).stable || emitted;
	}
	ASSERT_TRUE(emitted);
}

TEST(frame_static_mid_grey_text_still_settles)
{
	FrameProcessor processor(LiveConfig(120, 40));
	GrayscaleFrame frame = MakeGray(120, 40, 60);
	DrawGlyphLine(frame, 0, 150);
	bool emitted = false;
	for (int sample = 0; sample < 5; ++sample)
		emitted = processor.process(GrayView(frame), sample * 100).stable || emitted;
	ASSERT_TRUE(emitted);
}

// A long line typed out one character per sample must not be read until it is complete.
TEST(frame_long_line_typed_slowly_fires_only_when_complete)
{
	FrameProcessor processor(LiveConfig(640, 140));
	const int glyphs = 66;
	std::vector<int> fired_at;
	for (int sample = 0; sample < glyphs + 8; ++sample)
	{
		GrayscaleFrame frame = MakeGray(640, 140, 40);
		for (int glyph = 0; glyph < std::min(sample, glyphs); ++glyph)
			DrawGlyphAt(frame, glyph, 230);
		if (processor.process(GrayView(frame), sample * 100).stable)
			fired_at.push_back(sample);
	}
	ASSERT_EQ(fired_at.size(), static_cast<size_t>(1));
	ASSERT_TRUE(fired_at[0] >= glyphs);
}

// Grey text that never reaches the text-pixel threshold, next to a static white border:
// a new line must still start a new generation on a still screen.
TEST(frame_grey_text_change_next_to_a_white_border_is_read)
{
	FrameProcessor processor(LiveConfig(120, 40));
	std::vector<uint64_t> stable_generations;
	for (int sample = 0; sample < 12; ++sample)
	{
		GrayscaleFrame frame = MakeGray(120, 40, 60);
		DrawBorder(frame, 255);
		DrawGlyphLine(frame, sample < 6 ? 0 : 1, 150);
		const ProcessResult result = processor.process(GrayView(frame), sample * 100);
		if (result.stable)
			stable_generations.push_back(result.generation);
	}
	ASSERT_EQ(stable_generations.size(), static_cast<size_t>(2));
	ASSERT_TRUE(stable_generations[0] != stable_generations[1]);
}

TEST(frame_adaptive_threshold_is_binary)
{
	GrayscaleFrame frame = MakeGray(32, 32, 120);
	for (int y = 8; y < 24; ++y)
		for (int x = 8; x < 24; ++x)
			frame.pixels[static_cast<size_t>(y) * frame.stride + x] = 200;

	GrayscaleFrame thresholded;
	ASSERT_TRUE(adaptiveThreshold(frame, thresholded, 15, 8));
	for (uint8_t value : thresholded.pixels)
		ASSERT_TRUE(value == 0 || value == 255);

	// The bright square must survive as foreground somewhere in the middle.
	bool has_foreground = false;
	for (int y = 8; y < 24; ++y)
		for (int x = 8; x < 24; ++x)
			has_foreground = has_foreground ||
				thresholded.pixels[static_cast<size_t>(y) * thresholded.stride + x] == 255;
	ASSERT_TRUE(has_foreground);
}

TEST(frame_contrast_stretch_expands_range)
{
	GrayscaleFrame frame = MakeGray(16, 16, 100);
	for (int i = 0; i < 256; ++i)
		frame.pixels[i] = static_cast<uint8_t>(60 + i * 80 / 255);

	ASSERT_TRUE(enhanceContrast(frame, 1.0, 99.0));
	uint8_t lowest = 255;
	uint8_t highest = 0;
	for (uint8_t value : frame.pixels)
	{
		lowest = std::min(lowest, value);
		highest = std::max(highest, value);
	}
	ASSERT_TRUE(lowest <= 2);
	ASSERT_TRUE(highest >= 250);
}

TEST(ring_buffer_is_single_producer_single_consumer)
{
	SpscRingBuffer<int, 4> buffer;
	ASSERT_TRUE(buffer.tryPush(1));
	ASSERT_TRUE(buffer.tryPush(2));
	ASSERT_TRUE(buffer.tryPush(3));
	ASSERT_TRUE(buffer.tryPush(4));
	ASSERT_FALSE(buffer.tryPush(5));

	int value = 0;
	ASSERT_TRUE(buffer.tryPop(value));
	ASSERT_EQ(value, 1);
	ASSERT_TRUE(buffer.tryPush(5));

	for (int expected = 2; expected <= 5; ++expected)
	{
		ASSERT_TRUE(buffer.tryPop(value));
		ASSERT_EQ(value, expected);
	}
	ASSERT_FALSE(buffer.tryPop(value));
}

TEST(ring_buffer_keeps_shared_frames_alive)
{
	SpscRingBuffer<std::shared_ptr<int>, 2> buffer;
	auto frame = std::make_shared<int>(42);
	ASSERT_TRUE(buffer.tryPush(frame));
	frame.reset();

	std::shared_ptr<int> popped;
	ASSERT_TRUE(buffer.tryPop(popped));
	ASSERT_TRUE(popped != nullptr);
	ASSERT_EQ(*popped, 42);
}
