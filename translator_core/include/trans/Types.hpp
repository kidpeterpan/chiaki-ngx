// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Shared value types for the translator pipeline. Deliberately Qt-free so the
// core can be tested and cross-compiled without the GUI toolchain.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace trans {

enum class PixelFormat
{
	NV12,
	I420,
	P010,
	RGBA,
	BGRA,
	GRAY8,
};

struct Rect
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;

	bool empty() const { return width <= 0 || height <= 0; }
	int area() const { return empty() ? 0 : width * height; }
	int right() const { return x + width; }
	int bottom() const { return y + height; }
};

inline Rect intersect(const Rect &a, const Rect &b)
{
	const int left = a.x > b.x ? a.x : b.x;
	const int top = a.y > b.y ? a.y : b.y;
	const int right = a.right() < b.right() ? a.right() : b.right();
	const int bottom = a.bottom() < b.bottom() ? a.bottom() : b.bottom();
	return Rect{left, top, right - left, bottom - top};
}

struct Rgb
{
	uint8_t r = 0;
	uint8_t g = 0;
	uint8_t b = 0;
};

// Packed 8-bit RGB image. stride is always width * 3.
struct Image
{
	int width = 0;
	int height = 0;
	int stride = 0;
	std::vector<uint8_t> pixels;

	bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
	void clear()
	{
		width = height = stride = 0;
		pixels.clear();
	}

	Rgb pixel(int x, int y) const
	{
		const uint8_t *row = pixels.data() + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 3;
		return Rgb{row[0], row[1], row[2]};
	}

	uint8_t *mutableRow(int y) { return pixels.data() + static_cast<size_t>(y) * stride; }
	const uint8_t *row(int y) const { return pixels.data() + static_cast<size_t>(y) * stride; }
};

// Single-channel 8-bit image used for change detection and preprocessing.
struct GrayscaleFrame
{
	int width = 0;
	int height = 0;
	int stride = 0;
	std::vector<uint8_t> pixels;

	bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
	void clear()
	{
		width = height = stride = 0;
		pixels.clear();
	}

	uint8_t &at(int x, int y) { return pixels[static_cast<size_t>(y) * stride + x]; }
	uint8_t at(int x, int y) const { return pixels[static_cast<size_t>(y) * stride + x]; }
};

// Non-owning view of a decoded video frame. data[0] is luma for planar formats.
struct FrameView
{
	const uint8_t *data[3] = {nullptr, nullptr, nullptr};
	int stride[3] = {0, 0, 0};
	int width = 0;
	int height = 0;
	PixelFormat format = PixelFormat::NV12;
	int64_t pts_us = 0;

	bool valid() const { return width > 0 && height > 0 && data[0] != nullptr; }
};

struct OcrLine
{
	std::string text;
	float confidence = 0.0f;
	Rect box;          // axis-aligned box in the ROI image
	int angle = 0;      // 0 for horizontal, 90 for vertical text
	// Set when the OCR provider itself translated the region (Gemini Vision).
	// The pipeline delivers this instead of calling the translation backend.
	std::string translated_text;
};

struct TranslationItem
{
	uint64_t id = 0;
	std::string source_text;
	std::string translated_text;
	float ocr_confidence = 0.0f;
	int64_t source_timestamp_ms = 0;
	int64_t translated_timestamp_ms = 0;
	bool from_cache = false;
};

} // namespace trans
