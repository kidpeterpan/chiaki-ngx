// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Developer probe for the ONNX OCR backend. Reads a PPM image and prints the
// detected lines so model changes can be checked without the GUI.
//
// Build (macOS example):
//   clang++ -std=c++17 -DTRANS_HAVE_ONNXRUNTIME \
//     -I translator_core/include -I/opt/homebrew/include/onnxruntime \
//     translator_core/tools/ocr_probe.cpp translator_core/src/OnnxOcrEngine.cpp \
//     translator_core/src/OcrPostprocess.cpp -L/opt/homebrew/lib -lonnxruntime -o /tmp/ocr_probe
#include <trans/OcrEngine.hpp>
#include <trans/TextProcessor.hpp>

#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>

namespace {

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

trans::Image ReadPpm(const std::string &path)
{
	trans::Image image;
	std::ifstream stream(path, std::ios::binary);
	if (!stream)
	{
		std::fprintf(stderr, "cannot open %s\n", path.c_str());
		return image;
	}
	std::string magic;
	std::string width;
	std::string height;
	std::string maximum;
	if (!ReadToken(stream, magic) || magic != "P6" || !ReadToken(stream, width) ||
			!ReadToken(stream, height) || !ReadToken(stream, maximum))
	{
		std::fprintf(stderr, "invalid PPM header\n");
		return image;
	}
	const int w = std::stoi(width);
	const int h = std::stoi(height);
	image.width = w;
	image.height = h;
	image.stride = w * 3;
	image.pixels.resize(static_cast<size_t>(w) * h * 3);
	stream.read(reinterpret_cast<char *>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
	return image;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 5)
	{
		std::fprintf(stderr, "usage: %s <det.onnx> <rec.onnx> <dict.txt> <image.ppm>\n", argv[0]);
		return 2;
	}
	auto engine = trans::createOnnxOcrEngine();
	if (!engine)
	{
		std::fprintf(stderr, "this build has no ONNX Runtime support\n");
		return 2;
	}

	trans::OcrConfig config;
	config.detection_model = argv[1];
	config.recognition_model = argv[2];
	config.dictionary = argv[3];
	config.threads = 2;
	const auto load_begin = std::chrono::steady_clock::now();
	std::string error;
	if (!engine->load(config, &error))
	{
		std::fprintf(stderr, "load failed: %s\n", error.c_str());
		return 1;
	}

	const auto load_end = std::chrono::steady_clock::now();
	const trans::Image image = ReadPpm(argv[4]);
	if (image.empty())
		return 1;

	const auto recognize_begin = std::chrono::steady_clock::now();
	const std::vector<trans::OcrLine> lines = engine->recognize(image, &error);
	const auto recognize_end = std::chrono::steady_clock::now();
	std::fprintf(stderr, "load_ms=%.1f ocr_ms=%.1f\n",
		std::chrono::duration<double, std::milli>(load_end - load_begin).count(),
		std::chrono::duration<double, std::milli>(recognize_end - recognize_begin).count());
	if (!error.empty())
		std::fprintf(stderr, "recognize error: %s\n", error.c_str());
	for (const trans::OcrLine &line : lines)
	{
		std::printf("box=(%d,%d %dx%d) angle=%d conf=%.3f text=%s\n", line.box.x, line.box.y,
			line.box.width, line.box.height, line.angle, line.confidence, line.text.c_str());
	}
	std::printf("joined: %s\n", trans::joinOcrLines(lines).c_str());
	return lines.empty() ? 1 : 0;
}
