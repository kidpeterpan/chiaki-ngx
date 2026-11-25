// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/screenai.h"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <vector>

#if defined(Q_OS_LINUX)
#include <dlfcn.h>
#endif

namespace {

#if defined(Q_OS_LINUX)

struct SkColorInfo
{
	void *fColorSpace;
	int32_t fColorType;
	int32_t fAlphaType;
};

struct SkISize
{
	int32_t fWidth;
	int32_t fHeight;
};

struct SkImageInfo
{
	SkColorInfo fColorInfo;
	SkISize fDimensions;
};

struct SkPixmap
{
	void *fPixels;
	size_t fRowBytes;
	SkImageInfo fInfo;
};

struct SkBitmap
{
	void *fPixelRef;
	SkPixmap fPixmap;
	uint32_t fFlags;
};

using SetFileContentFunctionsFn = void (*)(void *, void *);
using InitOcrFn = bool (*)();
using SetLightModeFn = void (*)(bool);
using PerformOcrFn = void *(*)(SkBitmap *, uint32_t *);
using FreeCharArrayFn = void (*)(void *);
using MaxDimensionFn = uint32_t (*)();

std::string g_model_dir;
std::mutex g_model_dir_mutex;

uint32_t FileSizeCallback(const char *name)
{
	if (!name)
		return 0;
	std::lock_guard<std::mutex> lock(g_model_dir_mutex);
	const std::string path = g_model_dir + "/" + name;
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream)
		return 0;
	const std::streamoff size = stream.tellg();
	return size > 0 ? static_cast<uint32_t>(size) : 0;
}

void FileContentCallback(const char *name, uint32_t size, void *output)
{
	if (!name || !output)
		return;
	std::lock_guard<std::mutex> lock(g_model_dir_mutex);
	const std::string path = g_model_dir + "/" + name;
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
		return;
	stream.read(static_cast<char *>(output), static_cast<std::streamsize>(size));
}

bool ReadVarint(const uint8_t *&cursor, const uint8_t *end, uint64_t &value)
{
	value = 0;
	int shift = 0;
	while (cursor < end && shift < 64)
	{
		const uint8_t byte = *cursor++;
		value |= static_cast<uint64_t>(byte & 0x7f) << shift;
		if ((byte & 0x80) == 0)
			return true;
		shift += 7;
	}
	return false;
}

bool ReadLengthDelimited(const uint8_t *&cursor, const uint8_t *end,
	const uint8_t *&data, size_t &length)
{
	uint64_t value = 0;
	if (!ReadVarint(cursor, end, value))
		return false;
	if (value > static_cast<uint64_t>(end - cursor))
		return false;
	data = cursor;
	length = static_cast<size_t>(value);
	cursor += length;
	return true;
}

void SkipField(const uint8_t *&cursor, const uint8_t *end, uint32_t wire)
{
	switch (wire)
	{
	case 0:
	{
		uint64_t ignored = 0;
		ReadVarint(cursor, end, ignored);
		break;
	}
	case 1:
		cursor = std::min(end, cursor + 8);
		break;
	case 2:
	{
		const uint8_t *ignored = nullptr;
		size_t length = 0;
		ReadLengthDelimited(cursor, end, ignored, length);
		break;
	}
	case 5:
		cursor = std::min(end, cursor + 4);
		break;
	default:
		cursor = end;
		break;
	}
}

void ParseRect(const uint8_t *data, size_t length, trans::Rect &rect)
{
	const uint8_t *cursor = data;
	const uint8_t *end = data + length;
	while (cursor < end)
	{
		uint64_t key = 0;
		if (!ReadVarint(cursor, end, key))
			return;
		const uint32_t field = static_cast<uint32_t>(key >> 3);
		const uint32_t wire = static_cast<uint32_t>(key & 7);
		if (wire == 0)
		{
			uint64_t value = 0;
			if (!ReadVarint(cursor, end, value))
				return;
			switch (field)
			{
			case 1: rect.x = static_cast<int>(value); break;
			case 2: rect.y = static_cast<int>(value); break;
			case 3: rect.width = static_cast<int>(value); break;
			case 4: rect.height = static_cast<int>(value); break;
			default: break;
			}
		}
		else
			SkipField(cursor, end, wire);
	}
}

void ParseLine(const uint8_t *data, size_t length, std::vector<trans::OcrLine> &lines)
{
	trans::OcrLine line;
	const uint8_t *cursor = data;
	const uint8_t *end = data + length;
	while (cursor < end)
	{
		uint64_t key = 0;
		if (!ReadVarint(cursor, end, key))
			return;
		const uint32_t field = static_cast<uint32_t>(key >> 3);
		const uint32_t wire = static_cast<uint32_t>(key & 7);
		if (field == 2 && wire == 2)
		{
			const uint8_t *box = nullptr;
			size_t box_length = 0;
			if (!ReadLengthDelimited(cursor, end, box, box_length))
				return;
			ParseRect(box, box_length, line.box);
		}
		else if (field == 3 && wire == 2)
		{
			const uint8_t *text = nullptr;
			size_t text_length = 0;
			if (!ReadLengthDelimited(cursor, end, text, text_length))
				return;
			line.text.assign(reinterpret_cast<const char *>(text), text_length);
		}
		else if (field == 10 && wire == 5)
		{
			if (cursor + 4 > end)
				return;
			uint32_t bits = 0;
			std::memcpy(&bits, cursor, 4);
			float confidence = 0.0f;
			std::memcpy(&confidence, &bits, 4);
			line.confidence = confidence;
			cursor += 4;
		}
		else
			SkipField(cursor, end, wire);
	}
	if (!line.text.empty())
		lines.push_back(std::move(line));
}

std::vector<trans::OcrLine> ParseVisualAnnotation(const void *data, size_t length)
{
	std::vector<trans::OcrLine> lines;
	const uint8_t *cursor = static_cast<const uint8_t *>(data);
	const uint8_t *end = cursor + length;
	while (cursor < end)
	{
		uint64_t key = 0;
		if (!ReadVarint(cursor, end, key))
			break;
		const uint32_t field = static_cast<uint32_t>(key >> 3);
		const uint32_t wire = static_cast<uint32_t>(key & 7);
		if (field == 2 && wire == 2)
		{
			const uint8_t *line = nullptr;
			size_t line_length = 0;
			if (!ReadLengthDelimited(cursor, end, line, line_length))
				break;
			ParseLine(line, line_length, lines);
		}
		else
			SkipField(cursor, end, wire);
	}
	return lines;
}

#endif

} // namespace

ScreenAiEngine::~ScreenAiEngine()
{
	unload();
}

void ScreenAiEngine::unload()
{
#if defined(Q_OS_LINUX)
	if (library_)
	{
		dlclose(library_);
		library_ = nullptr;
	}
#endif
	ready_ = false;
}

bool ScreenAiEngine::load(const trans::OcrConfig &config, std::string *error)
{
	unload();
#if !defined(Q_OS_LINUX)
	(void)config;
	if (error)
		*error = "Chrome Screen AI is supported only on Linux x86_64";
	return false;
#else
	model_dir_ = config.screenai_model_dir;
	if (model_dir_.empty())
	{
		if (error)
			*error = "Chrome Screen AI model directory is not configured";
		return false;
	}
	const std::string library_path = model_dir_ + "/libchromescreenai.so";
	if (!std::ifstream(library_path).good())
	{
		if (error)
			*error = "Chrome Screen AI assets are missing; run scripts/translator/fetch-screenai.sh";
		return false;
	}

	void *handle = dlopen(library_path.c_str(), RTLD_LAZY | RTLD_LOCAL);
	if (!handle)
	{
		if (error)
			*error = std::string("Cannot load libchromescreenai.so: ") + dlerror();
		return false;
	}
	auto resolve = [handle](const char *symbol) -> void * {
		return dlsym(handle, symbol);
	};
	auto set_functions = reinterpret_cast<SetFileContentFunctionsFn>(resolve("SetFileContentFunctions"));
	auto init = reinterpret_cast<InitOcrFn>(resolve("InitOCRUsingCallback"));
	auto set_light = reinterpret_cast<SetLightModeFn>(resolve("SetOCRLightMode"));
	auto perform = reinterpret_cast<PerformOcrFn>(resolve("PerformOCR"));
	auto free_chars = reinterpret_cast<FreeCharArrayFn>(resolve("FreeLibraryAllocatedCharArray"));
	auto max_dim = reinterpret_cast<MaxDimensionFn>(resolve("GetMaxImageDimension"));
	if (!set_functions || !init || !set_light || !perform || !free_chars || !max_dim)
	{
		dlclose(handle);
		if (error)
			*error = "libchromescreenai.so is missing required symbols";
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_model_dir_mutex);
		g_model_dir = model_dir_;
	}
	set_functions(reinterpret_cast<void *>(FileSizeCallback),
		reinterpret_cast<void *>(FileContentCallback));
	if (!init())
	{
		dlclose(handle);
		if (error)
			*error = "Chrome Screen AI InitOCRUsingCallback failed";
		return false;
	}
	set_light(false);
	max_dimension_ = max_dim();
	if (max_dimension_ == 0)
		max_dimension_ = 2048;
	library_ = handle;
	perform_ = reinterpret_cast<void *>(perform);
	free_ = reinterpret_cast<void *>(free_chars);
	ready_ = true;
	return true;
#endif
}

std::vector<trans::OcrLine> ScreenAiEngine::recognize(const trans::Image &image, std::string *error)
{
	std::vector<trans::OcrLine> lines;
#if !defined(Q_OS_LINUX)
	(void)image;
	if (error)
		*error = "Chrome Screen AI is supported only on Linux x86_64";
	return lines;
#else
	if (!ready_ || image.empty())
		return lines;

	int width = image.width;
	int height = image.height;
	double scale = 1.0;
	if (static_cast<unsigned>(std::max(width, height)) > max_dimension_)
	{
		scale = static_cast<double>(max_dimension_) / std::max(width, height);
		width = std::max(1, static_cast<int>(width * scale));
		height = std::max(1, static_cast<int>(height * scale));
	}
	std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
	for (int y = 0; y < height; ++y)
	{
		const int source_y = std::min(image.height - 1, static_cast<int>(y / scale));
		const uint8_t *row = image.row(source_y);
		for (int x = 0; x < width; ++x)
		{
			const int source_x = std::min(image.width - 1, static_cast<int>(x / scale));
			uint8_t *pixel = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
			pixel[0] = row[source_x * 3 + 0];
			pixel[1] = row[source_x * 3 + 1];
			pixel[2] = row[source_x * 3 + 2];
			pixel[3] = 255;
		}
	}

	SkBitmap bitmap{};
	bitmap.fPixmap.fPixels = rgba.data();
	bitmap.fPixmap.fRowBytes = static_cast<size_t>(width) * 4;
	bitmap.fPixmap.fInfo.fColorInfo.fColorType = 4; // kRGBA_8888
	bitmap.fPixmap.fInfo.fColorInfo.fAlphaType = 1; // kOpaque
	bitmap.fPixmap.fInfo.fDimensions.fWidth = width;
	bitmap.fPixmap.fInfo.fDimensions.fHeight = height;

	auto perform = reinterpret_cast<PerformOcrFn>(perform_);
	auto free_chars = reinterpret_cast<FreeCharArrayFn>(free_);
	uint32_t length = 0;
	void *data = perform(&bitmap, &length);
	if (!data || length == 0)
	{
		if (error)
			*error = "Chrome Screen AI returned no result";
		return lines;
	}
	lines = ParseVisualAnnotation(data, length);
	free_chars(data);

	if (scale != 1.0)
	{
		for (trans::OcrLine &line : lines)
		{
			line.box.x = static_cast<int>(line.box.x / scale);
			line.box.y = static_cast<int>(line.box.y / scale);
			line.box.width = static_cast<int>(line.box.width / scale);
			line.box.height = static_cast<int>(line.box.height / scale);
		}
	}
	if (lines.empty() && error)
		*error = "Chrome Screen AI returned no text";
	return lines;
#endif
}
