// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Model-independent pieces of the OCR pipeline: DB text detection
// post-processing, CTC decoding, and the crop/rotate helpers used to feed
// vertical Japanese text through a horizontal recognizer.
#pragma once

#include <trans/Types.hpp>

#include <string>
#include <vector>

namespace trans {

struct CtcResult
{
	std::string text;
	float confidence = 0.0f;
	std::vector<int> class_indices;
};

class CtcDecoder
{
public:
	void setDictionary(std::vector<std::string> dictionary);
	bool loadDictionary(const std::string &path, std::string *error);
	size_t dictionarySize() const { return dictionary_.size(); }

	// logits is row-major [timesteps][classes]. Class 0 is the CTC blank and
	// classes 1..N map to dictionary entries 0..N-1.
	CtcResult decode(const float *logits, int timesteps, int classes) const;

private:
	std::vector<std::string> dictionary_;
};

struct DbConfig
{
	float threshold = 0.3f;
	float box_threshold = 0.5f;
	float unclip_ratio = 1.6f;
	int min_size = 3;
	int max_candidates = 1000;
};

struct DetBox
{
	Rect box;
	float score = 0.0f;
};

// probability is row-major [height][width]. Coordinates are scaled to
// target_width/target_height (the original ROI) when both are positive.
std::vector<DetBox> dbPostProcess(const float *probability, int width, int height,
		const DbConfig &config, int target_width = 0, int target_height = 0);

int preferredRotation(const Rect &box);

// Groups upright character boxes that belong to one vertical Japanese column
// and concatenates them top-to-bottom. Wide subtitle boxes are left alone.
std::vector<OcrLine> mergeVerticalLines(const std::vector<OcrLine> &lines);

// A dialogue block: one or more OCR lines that belong to the same on-screen
// text box. Boxes are in ROI-image coordinates.
struct OcrBlock
{
	std::string text;
	float confidence = 0.0f;
	Rect box;
	std::vector<OcrLine> lines;
};

// Groups side-by-side choice boxes separately while merging wrapped subtitle
// lines that overlap horizontally and sit close vertically.
std::vector<OcrBlock> groupOcrLines(const std::vector<OcrLine> &lines);

Image cropImage(const Image &image, const Rect &roi);
Image rotate90Clockwise(const Image &image);
Image rotate90CounterClockwise(const Image &image);

} // namespace trans
