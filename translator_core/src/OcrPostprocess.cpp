// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/OcrPostprocess.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace trans {

void CtcDecoder::setDictionary(std::vector<std::string> dictionary)
{
	dictionary_ = std::move(dictionary);
}

bool CtcDecoder::loadDictionary(const std::string &path, std::string *error)
{
	std::ifstream stream(path);
	if (!stream)
	{
		if (error)
			*error = "cannot open OCR dictionary: " + path;
		return false;
	}

	std::vector<std::string> dictionary;
	std::string line;
	while (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		dictionary.push_back(line);
	}
	if (dictionary.empty())
	{
		if (error)
			*error = "OCR dictionary is empty: " + path;
		return false;
	}
	if (error)
		error->clear();
	dictionary_ = std::move(dictionary);
	return true;
}

CtcResult CtcDecoder::decode(const float *logits, int timesteps, int classes) const
{
	CtcResult result;
	if (logits == nullptr || timesteps <= 0 || classes <= 0)
		return result;

	int previous = -1;
	double confidence_sum = 0.0;
	int confidence_count = 0;

	for (int t = 0; t < timesteps; ++t)
	{
		const float *row = logits + static_cast<size_t>(t) * classes;
		int best = 0;
		float best_logit = row[0];
		for (int c = 1; c < classes; ++c)
		{
			if (row[c] > best_logit)
			{
				best_logit = row[c];
				best = c;
			}
		}

		// Some exported recognition models already contain softmax and emit
		// probabilities; others emit logits. Detect per row instead of guessing.
		double probability = 0.0;
		double logit_sum = 0.0;
		bool looks_like_probabilities = best_logit <= 1.0001f;
		for (int c = 0; c < classes && looks_like_probabilities; ++c)
		{
			if (row[c] < -1e-4f)
				looks_like_probabilities = false;
			logit_sum += row[c];
		}
		if (looks_like_probabilities && logit_sum > 0.9 && logit_sum < 1.1)
		{
			probability = best_logit;
		}
		else
		{
			double sum = 0.0;
			for (int c = 0; c < classes; ++c)
				sum += std::exp(static_cast<double>(row[c] - best_logit));
			probability = sum > 0.0 ? 1.0 / sum : 0.0;
		}

		if (best == previous)
			continue;
		previous = best;
		if (best == 0)
			continue;
		const int dictionary_index = best - 1;
		if (static_cast<size_t>(dictionary_index) >= dictionary_.size())
			continue;

		result.text += dictionary_[dictionary_index];
		result.class_indices.push_back(best);
		confidence_sum += probability;
		++confidence_count;
	}

	if (confidence_count > 0)
		result.confidence = static_cast<float>(confidence_sum / confidence_count);
	return result;
}

std::vector<DetBox> dbPostProcess(const float *probability, int width, int height,
		const DbConfig &config, int target_width, int target_height)
{
	std::vector<DetBox> boxes;
	if (probability == nullptr || width <= 0 || height <= 0)
		return boxes;

	const float threshold = std::clamp(config.threshold, 0.0f, 1.0f);
	const float box_threshold = std::clamp(config.box_threshold, 0.0f, 1.0f);
	const int min_size = std::max(1, config.min_size);
	const float unclip_ratio = std::max(0.0f, config.unclip_ratio);
	const int max_candidates = std::max(1, config.max_candidates);

	std::vector<uint8_t> visited(static_cast<size_t>(width) * height, 0);
	std::vector<int> stack;
	stack.reserve(1024);

	for (int seed_y = 0; seed_y < height; ++seed_y)
	{
		for (int seed_x = 0; seed_x < width; ++seed_x)
		{
			const int seed = seed_y * width + seed_x;
			if (visited[static_cast<size_t>(seed)] || probability[seed] < threshold)
				continue;

			int min_x = seed_x;
			int max_x = seed_x;
			int min_y = seed_y;
			int max_y = seed_y;
			double score_sum = 0.0;
			int count = 0;

			stack.clear();
			stack.push_back(seed);
			visited[static_cast<size_t>(seed)] = 1;
			while (!stack.empty())
			{
				const int index = stack.back();
				stack.pop_back();
				const int x = index % width;
				const int y = index / width;
				min_x = std::min(min_x, x);
				max_x = std::max(max_x, x);
				min_y = std::min(min_y, y);
				max_y = std::max(max_y, y);
				score_sum += probability[index];
				++count;

				const int neighbours[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
				for (const auto &neighbour : neighbours)
				{
					const int nx = neighbour[0];
					const int ny = neighbour[1];
					if (nx < 0 || ny < 0 || nx >= width || ny >= height)
						continue;
					const int neighbour_index = ny * width + nx;
					if (visited[static_cast<size_t>(neighbour_index)] ||
							probability[neighbour_index] < threshold)
						continue;
					visited[static_cast<size_t>(neighbour_index)] = 1;
					stack.push_back(neighbour_index);
				}
			}

			const int box_width = max_x - min_x + 1;
			const int box_height = max_y - min_y + 1;
			if (box_width < min_size || box_height < min_size)
				continue;

			const float score = count > 0 ? static_cast<float>(score_sum / count) : 0.0f;
			if (score < box_threshold)
				continue;

			Rect box{min_x, min_y, box_width, box_height};
			const double perimeter = 2.0 * (box_width + box_height);
			const double offset = perimeter > 0.0
				? static_cast<double>(box_width) * box_height * unclip_ratio / perimeter
				: 0.0;
			const int expand = static_cast<int>(std::lround(offset));
			box.x -= expand;
			box.y -= expand;
			box.width += expand * 2;
			box.height += expand * 2;

			if (target_width > 0 && target_height > 0 && (target_width != width || target_height != height))
			{
				const double scale_x = static_cast<double>(target_width) / width;
				const double scale_y = static_cast<double>(target_height) / height;
				const int x0 = static_cast<int>(std::lround(box.x * scale_x));
				const int y0 = static_cast<int>(std::lround(box.y * scale_y));
				const int x1 = static_cast<int>(std::lround((box.x + box.width) * scale_x));
				const int y1 = static_cast<int>(std::lround((box.y + box.height) * scale_y));
				box = Rect{std::max(0, x0), std::max(0, y0),
					std::max(1, x1 - std::max(0, x0)), std::max(1, y1 - std::max(0, y0))};
				box.width = std::min(box.width, target_width - box.x);
				box.height = std::min(box.height, target_height - box.y);
			}

			boxes.push_back(DetBox{box, score});
			if (static_cast<int>(boxes.size()) >= max_candidates)
				return boxes;
		}
	}

	std::sort(boxes.begin(), boxes.end(), [](const DetBox &a, const DetBox &b) {
		if (a.box.y != b.box.y)
			return a.box.y < b.box.y;
		return a.box.x < b.box.x;
	});
	return boxes;
}


std::vector<OcrLine> mergeVerticalLines(const std::vector<OcrLine> &lines)
{
	struct Group
	{
		std::vector<size_t> members;
		Rect bounds;
	};

	std::vector<bool> consumed(lines.size(), false);
	std::vector<Group> groups;
	for (size_t index = 0; index < lines.size(); ++index)
	{
		const OcrLine &line = lines[index];
		if (line.text.empty() || line.box.empty())
			continue;
		const double width = std::max(1, line.box.width);
		const double height = std::max(1, line.box.height);
		if (height < width * 0.75 || height > width * 2.5)
			continue;

		const int center_x = line.box.x + line.box.width / 2;
		int chosen = -1;
		for (size_t group_index = 0; group_index < groups.size(); ++group_index)
		{
			const Rect &bounds = groups[group_index].bounds;
			const int group_center = bounds.x + bounds.width / 2;
			const int tolerance = static_cast<int>(std::max(24.0, std::max(width, static_cast<double>(bounds.width)) * 0.6));
			if (std::abs(center_x - group_center) > tolerance)
				continue;
			const int gap = line.box.y - (bounds.y + bounds.height);
			const int max_gap = static_cast<int>(std::max(24.0, height * 0.8));
			if (gap > max_gap)
				continue;
			chosen = static_cast<int>(group_index);
			break;
		}

		if (chosen < 0)
		{
			Group group;
			group.members.push_back(index);
			group.bounds = line.box;
			groups.push_back(group);
		}
		else
		{
			Group &group = groups[static_cast<size_t>(chosen)];
			group.members.push_back(index);
			const int right = std::max(group.bounds.right(), line.box.right());
			const int bottom = std::max(group.bounds.bottom(), line.box.bottom());
			group.bounds.x = std::min(group.bounds.x, line.box.x);
			group.bounds.y = std::min(group.bounds.y, line.box.y);
			group.bounds.width = right - group.bounds.x;
			group.bounds.height = bottom - group.bounds.y;
		}
	}

	std::vector<OcrLine> merged;
	std::vector<bool> emitted(lines.size(), false);
	for (const Group &group : groups)
	{
		if (group.members.size() < 2)
			continue;
		std::vector<size_t> ordered = group.members;
		std::sort(ordered.begin(), ordered.end(), [&lines](size_t a, size_t b) {
			return lines[a].box.y < lines[b].box.y;
		});
		OcrLine column;
		column.box = group.bounds;
		column.angle = 90;
		double confidence_sum = 0.0;
		for (size_t member : ordered)
		{
			column.text += lines[member].text;
			confidence_sum += lines[member].confidence;
			consumed[member] = true;
		}
		column.confidence = static_cast<float>(confidence_sum / ordered.size());
		for (size_t member : ordered)
			emitted[member] = true;
		merged.push_back(column);
	}

	std::vector<OcrLine> result;
	for (size_t index = 0; index < lines.size(); ++index)
	{
		if (emitted[index])
			continue;
		result.push_back(lines[index]);
	}
	result.insert(result.end(), merged.begin(), merged.end());
	std::sort(result.begin(), result.end(), [](const OcrLine &a, const OcrLine &b) {
		if (a.box.y != b.box.y)
			return a.box.y < b.box.y;
		return a.box.x < b.box.x;
	});
	return result;
}

int preferredRotation(const Rect &box)
{
	if (box.height >= 2 * std::max(1, box.width))
		return 90;
	return 0;
}

Image cropImage(const Image &image, const Rect &roi)
{
	Image cropped;
	if (image.empty())
		return cropped;
	const Rect clipped = intersect(roi, Rect{0, 0, image.width, image.height});
	if (clipped.empty())
		return cropped;

	cropped.width = clipped.width;
	cropped.height = clipped.height;
	cropped.stride = clipped.width * 3;
	cropped.pixels.resize(static_cast<size_t>(cropped.width) * cropped.height * 3);
	for (int y = 0; y < clipped.height; ++y)
	{
		const uint8_t *source = image.row(clipped.y + y) + static_cast<size_t>(clipped.x) * 3;
		std::copy(source, source + static_cast<size_t>(clipped.width) * 3,
			cropped.mutableRow(y));
	}
	return cropped;
}

Image rotate90Clockwise(const Image &image)
{
	Image rotated;
	if (image.empty())
		return rotated;
	rotated.width = image.height;
	rotated.height = image.width;
	rotated.stride = rotated.width * 3;
	rotated.pixels.resize(static_cast<size_t>(rotated.width) * rotated.height * 3);

	for (int y = 0; y < image.height; ++y)
	{
		const uint8_t *source_row = image.row(y);
		for (int x = 0; x < image.width; ++x)
		{
			const int destination_x = image.height - 1 - y;
			const int destination_y = x;
			std::copy(source_row + x * 3, source_row + x * 3 + 3,
				rotated.mutableRow(destination_y) + static_cast<size_t>(destination_x) * 3);
		}
	}
	return rotated;
}

Image rotate90CounterClockwise(const Image &image)
{
	Image rotated;
	if (image.empty())
		return rotated;
	rotated.width = image.height;
	rotated.height = image.width;
	rotated.stride = rotated.width * 3;
	rotated.pixels.resize(static_cast<size_t>(rotated.width) * rotated.height * 3);

	for (int y = 0; y < image.height; ++y)
	{
		const uint8_t *source_row = image.row(y);
		for (int x = 0; x < image.width; ++x)
		{
			const int destination_x = y;
			const int destination_y = image.width - 1 - x;
			std::copy(source_row + x * 3, source_row + x * 3 + 3,
				rotated.mutableRow(destination_y) + static_cast<size_t>(destination_x) * 3);
		}
	}
	return rotated;
}

} // namespace trans
