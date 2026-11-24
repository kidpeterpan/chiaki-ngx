// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <trans/OcrEngine.hpp>
#include <trans/OcrPostprocess.hpp>

#include <cstdio>
#include <fstream>
#include <vector>

using namespace trans;

namespace {

Image MakeTestImage(int width, int height)
{
	Image image;
	image.width = width;
	image.height = height;
	image.stride = width * 3;
	image.pixels.resize(static_cast<size_t>(width) * height * 3);
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			uint8_t *pixel = image.mutableRow(y) + x * 3;
			pixel[0] = static_cast<uint8_t>(x * 7 + y);
			pixel[1] = static_cast<uint8_t>(x);
			pixel[2] = static_cast<uint8_t>(y);
		}
	}
	return image;
}

} // namespace

TEST(ocr_ctc_decodes_blank_separated_classes)
{
	CtcDecoder decoder;
	decoder.setDictionary({"あ", "い", "う", "え"});

	const int timesteps = 6;
	const int classes = 5;
	std::vector<float> logits(static_cast<size_t>(timesteps) * classes, 0.0f);
	const int path[timesteps] = {0, 1, 1, 0, 3, 0};
	for (int t = 0; t < timesteps; ++t)
		logits[static_cast<size_t>(t) * classes + path[t]] = 10.0f;

	const CtcResult result = decoder.decode(logits.data(), timesteps, classes);
	ASSERT_EQ(result.text, "あう");
	ASSERT_TRUE(result.confidence > 0.9f);
	ASSERT_TRUE(result.confidence <= 1.0f);
}

TEST(ocr_ctc_collapses_repeated_frames_but_keeps_blank_separated_repeats)
{
	CtcDecoder decoder;
	decoder.setDictionary({"ア", "イ"});

	const int timesteps = 8;
	const int classes = 3;
	std::vector<float> logits(static_cast<size_t>(timesteps) * classes, 0.0f);
	const int path[timesteps] = {1, 1, 1, 0, 1, 1, 0, 2};
	for (int t = 0; t < timesteps; ++t)
		logits[static_cast<size_t>(t) * classes + path[t]] = 10.0f;

	const CtcResult result = decoder.decode(logits.data(), timesteps, classes);
	ASSERT_EQ(result.text, "アアイ");
}

TEST(ocr_ctc_blank_only_returns_empty_text)
{
	CtcDecoder decoder;
	decoder.setDictionary({"あ"});
	std::vector<float> logits(static_cast<size_t>(10), 0.0f);
	logits[0] = 10.0f;
	const CtcResult result = decoder.decode(logits.data(), 10, 1);
	ASSERT_TRUE(result.text.empty());
	ASSERT_NEAR(result.confidence, 0.0f, 1e-6);
}

TEST(ocr_ctc_loading_dictionary_from_file)
{
	const std::string path = "/tmp/translator_core_dict_test.txt";
	{
		std::ofstream file(path);
		file << "a\nb\nc\n";
	}
	CtcDecoder decoder;
	std::string error;
	ASSERT_TRUE(decoder.loadDictionary(path, &error));
	ASSERT_TRUE(error.empty());
	ASSERT_EQ(decoder.dictionarySize(), static_cast<size_t>(3));

	std::vector<float> logits(static_cast<size_t>(3) * 4, 0.0f);
	logits[0 * 4 + 1] = 10.0f;
	logits[1 * 4 + 2] = 10.0f;
	logits[2 * 4 + 3] = 10.0f;
	ASSERT_EQ(decoder.decode(logits.data(), 3, 4).text, "abc");
}

TEST(ocr_db_finds_bright_region_and_scores_it)
{
	const int height = 32;
	const int width = 32;
	std::vector<float> probability(static_cast<size_t>(height) * width, 0.0f);
	for (int y = 4; y < 12; ++y)
		for (int x = 8; x < 24; ++x)
			probability[static_cast<size_t>(y) * width + x] = 0.9f;

	DbConfig config;
	config.threshold = 0.3f;
	config.box_threshold = 0.5f;
	config.unclip_ratio = 1.6f;
	config.min_size = 3;

	const std::vector<DetBox> boxes = dbPostProcess(probability.data(), width, height, config, 160, 80);
	ASSERT_EQ(boxes.size(), static_cast<size_t>(1));
	const DetBox &box = boxes[0];
	ASSERT_TRUE(box.score > 0.85f);
	ASSERT_NEAR(box.box.x + box.box.width / 2.0, 80.0, 6.0);
	ASSERT_NEAR(box.box.y + box.box.height / 2.0, 20.0, 6.0);
	ASSERT_TRUE(box.box.width >= 70);
	ASSERT_TRUE(box.box.height >= 30);
}

TEST(ocr_db_rejects_weak_region_below_box_threshold)
{
	const int height = 16;
	const int width = 16;
	std::vector<float> probability(static_cast<size_t>(height) * width, 0.0f);
	for (int y = 4; y < 12; ++y)
		for (int x = 4; x < 12; ++x)
			probability[static_cast<size_t>(y) * width + x] = 0.4f;

	DbConfig config;
	config.threshold = 0.3f;
	config.box_threshold = 0.5f;
	ASSERT_TRUE(dbPostProcess(probability.data(), width, height, config, 16, 16).empty());
}

TEST(ocr_db_keeps_separate_components_apart)
{
	const int height = 20;
	const int width = 40;
	std::vector<float> probability(static_cast<size_t>(height) * width, 0.0f);
	for (int y = 5; y < 15; ++y)
	{
		for (int x = 3; x < 13; ++x)
			probability[static_cast<size_t>(y) * width + x] = 0.95f;
		for (int x = 25; x < 35; ++x)
			probability[static_cast<size_t>(y) * width + x] = 0.95f;
	}

	DbConfig config;
	config.min_size = 3;
	config.box_threshold = 0.5f;
	const std::vector<DetBox> boxes = dbPostProcess(probability.data(), width, height, config, 80, 40);
	ASSERT_EQ(boxes.size(), static_cast<size_t>(2));
	ASSERT_TRUE(boxes[0].box.x < boxes[1].box.x);
}

TEST(ocr_rotation_preference_detects_vertical_text)
{
	ASSERT_EQ(preferredRotation(Rect{0, 0, 200, 30}), 0);
	ASSERT_EQ(preferredRotation(Rect{0, 0, 30, 200}), 90);
	ASSERT_EQ(preferredRotation(Rect{0, 0, 60, 90}), 0);
}

TEST(ocr_image_crop_and_rotate_are_pixel_exact)
{
	const Image image = MakeTestImage(5, 3);
	const Image cropped = cropImage(image, Rect{1, 1, 2, 2});
	ASSERT_EQ(cropped.width, 2);
	ASSERT_EQ(cropped.height, 2);
	ASSERT_EQ(static_cast<int>(cropped.pixel(0, 0).r), static_cast<int>(image.pixel(1, 1).r));

	const Image rotated = rotate90Clockwise(cropped);
	ASSERT_EQ(rotated.width, 2);
	ASSERT_EQ(rotated.height, 2);
	ASSERT_EQ(static_cast<int>(rotated.pixel(0, 0).r), static_cast<int>(cropped.pixel(0, 1).r));
	ASSERT_EQ(static_cast<int>(rotated.pixel(1, 0).r), static_cast<int>(cropped.pixel(0, 0).r));
}

TEST(ocr_null_engine_reports_missing_runtime)
{
	auto engine = createNullOcrEngine();
	ASSERT_TRUE(engine != nullptr);
	ASSERT_FALSE(engine->ready());
	std::string error;
	const std::vector<OcrLine> lines = engine->recognize(MakeTestImage(10, 10), &error);
	ASSERT_TRUE(lines.empty());
	ASSERT_FALSE(error.empty());
}

TEST(ocr_known_dictionary_characters_are_kept)
{
	// Guards against a decoder that silently drops non-ASCII class labels.
	CtcDecoder decoder;
	decoder.setDictionary({"日", "本", "語"});
	std::vector<float> logits(static_cast<size_t>(3) * 4, 0.0f);
	for (int t = 0; t < 3; ++t)
		logits[static_cast<size_t>(t) * 4 + (t + 1)] = 10.0f;
	ASSERT_EQ(decoder.decode(logits.data(), 3, 4).text, "日本語");
}

TEST(ocr_merges_upright_vertical_character_boxes)
{
	std::vector<OcrLine> lines;
	const char *texts[] = {"こ", "ん", "に", "ち", "は"};
	for (int index = 0; index < 5; ++index)
	{
		OcrLine line;
		line.text = texts[index];
		line.confidence = 0.9f;
		line.box = Rect{48, 10 + index * 52, 44, 46};
		lines.push_back(line);
	}
	OcrLine subtitle;
	subtitle.text = "ゲームを続けよう";
	subtitle.confidence = 0.95f;
	subtitle.box = Rect{20, 400, 480, 48};
	lines.push_back(subtitle);

	const std::vector<OcrLine> merged = mergeVerticalLines(lines);
	ASSERT_EQ(merged.size(), static_cast<size_t>(2));
	ASSERT_EQ(merged[0].text, "こんにちは");
	ASSERT_EQ(merged[0].angle, 90);
	ASSERT_EQ(merged[0].box.height, 5 * 52 - 6);
	ASSERT_EQ(merged[1].text, "ゲームを続けよう");
}

TEST(ocr_does_not_merge_separate_vertical_columns)
{
	std::vector<OcrLine> lines;
	OcrLine left;
	left.text = "あ";
	left.box = Rect{20, 10, 40, 40};
	OcrLine left2;
	left2.text = "い";
	left2.box = Rect{20, 60, 40, 40};
	OcrLine right;
	right.text = "う";
	right.box = Rect{140, 10, 40, 40};
	OcrLine right2;
	right2.text = "え";
	right2.box = Rect{140, 60, 40, 40};
	lines = {left, left2, right, right2};

	const std::vector<OcrLine> merged = mergeVerticalLines(lines);
	ASSERT_EQ(merged.size(), static_cast<size_t>(2));
	ASSERT_EQ(merged[0].text, "あい");
	ASSERT_EQ(merged[1].text, "うえ");
}
