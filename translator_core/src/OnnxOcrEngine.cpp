// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// PaddleOCR (PP-OCR) inference through the ONNX Runtime C++ API.
//
// Detection model: DB segmentation, output [1,1,H,W] probability map.
// Recognition model: CTC, input [1,3,48,W], output [1,T,classes].
// Builds without ONNX Runtime keep a null factory so the rest of the pipeline
// still compiles and the GUI can report a missing engine.
#include <trans/OcrEngine.hpp>

#include <trans/OcrPostprocess.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#if defined(TRANS_HAVE_ONNXRUNTIME)

#include <onnxruntime_cxx_api.h>

namespace trans {
namespace {

constexpr float kDetMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kDetStd[3] = {0.229f, 0.224f, 0.225f};

Image ResizeBilinear(const Image &source, int width, int height)
{
	Image target;
	if (source.empty() || width <= 0 || height <= 0)
		return target;
	target.width = width;
	target.height = height;
	target.stride = width * 3;
	target.pixels.resize(static_cast<size_t>(width) * height * 3);

	const double scale_x = static_cast<double>(source.width) / width;
	const double scale_y = static_cast<double>(source.height) / height;
	for (int y = 0; y < height; ++y)
	{
		const double source_y = (y + 0.5) * scale_y - 0.5;
		const int y0 = std::clamp(static_cast<int>(std::floor(source_y)), 0, source.height - 1);
		const int y1 = std::min(y0 + 1, source.height - 1);
		const double wy = std::clamp(source_y - y0, 0.0, 1.0);
		const uint8_t *row0 = source.row(y0);
		const uint8_t *row1 = source.row(y1);
		uint8_t *destination = target.mutableRow(y);
		for (int x = 0; x < width; ++x)
		{
			const double source_x = (x + 0.5) * scale_x - 0.5;
			const int x0 = std::clamp(static_cast<int>(std::floor(source_x)), 0, source.width - 1);
			const int x1 = std::min(x0 + 1, source.width - 1);
			const double wx = std::clamp(source_x - x0, 0.0, 1.0);
			for (int channel = 0; channel < 3; ++channel)
			{
				const double top = row0[x0 * 3 + channel] * (1.0 - wx) + row0[x1 * 3 + channel] * wx;
				const double bottom = row1[x0 * 3 + channel] * (1.0 - wx) + row1[x1 * 3 + channel] * wx;
				destination[x * 3 + channel] = static_cast<uint8_t>(
					std::clamp(top * (1.0 - wy) + bottom * wy, 0.0, 255.0));
			}
		}
	}
	return target;
}

struct DetectionTensor
{
	std::vector<float> data;
	int width = 0;
	int height = 0;
};

DetectionTensor PrepareDetection(const Image &image, int limit_side)
{
	DetectionTensor tensor;
	if (image.empty())
		return tensor;

	const int longest = std::max(image.width, image.height);
	double ratio = 1.0;
	if (limit_side > 0 && longest > limit_side)
		ratio = static_cast<double>(limit_side) / longest;
	int width = std::max(32, static_cast<int>(std::lround(image.width * ratio / 32.0)) * 32);
	int height = std::max(32, static_cast<int>(std::lround(image.height * ratio / 32.0)) * 32);
	const int max_side = std::max(width, height);
	if (limit_side > 0 && max_side > limit_side)
	{
		const double correction = static_cast<double>(limit_side) / max_side;
		width = std::max(32, static_cast<int>(width * correction) / 32 * 32);
		height = std::max(32, static_cast<int>(height * correction) / 32 * 32);
	}

	const Image resized = ResizeBilinear(image, width, height);
	tensor.width = width;
	tensor.height = height;
	tensor.data.resize(static_cast<size_t>(3) * width * height);
	const size_t plane = static_cast<size_t>(width) * height;
	for (int y = 0; y < height; ++y)
	{
		const uint8_t *row = resized.row(y);
		for (int x = 0; x < width; ++x)
		{
			for (int channel = 0; channel < 3; ++channel)
			{
				const float value = row[x * 3 + channel] / 255.0f;
				tensor.data[static_cast<size_t>(channel) * plane + static_cast<size_t>(y) * width + x] =
					(value - kDetMean[channel]) / kDetStd[channel];
			}
		}
	}
	return tensor;
}

std::vector<float> PrepareRecognition(const Image &image, int height, int &out_width)
{
	out_width = 0;
	if (image.empty() || image.height <= 0)
		return {};
	int width = static_cast<int>(std::lround(static_cast<double>(image.width) * height / image.height));
	width = std::clamp(width, 16, 1280);
	const Image resized = ResizeBilinear(image, width, height);
	if (resized.empty())
		return {};

	out_width = width;
	const size_t plane = static_cast<size_t>(width) * height;
	std::vector<float> data(plane * 3);
	for (int y = 0; y < height; ++y)
	{
		const uint8_t *row = resized.row(y);
		for (int x = 0; x < width; ++x)
		{
			for (int channel = 0; channel < 3; ++channel)
			{
				data[static_cast<size_t>(channel) * plane + static_cast<size_t>(y) * width + x] =
					(row[x * 3 + channel] / 255.0f - 0.5f) / 0.5f;
			}
		}
	}
	return data;
}

std::vector<std::string> ReadDictionary(const std::string &path, std::string *error)
{
	std::vector<std::string> dictionary;
	std::ifstream stream(path);
	if (!stream)
	{
		if (error)
			*error = "cannot open OCR dictionary: " + path;
		return dictionary;
	}
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
		return dictionary;
	}
	// PP-OCR recognition models trained with use_space_char expose one extra
	// class for the space token before the CTC blank.
	if (dictionary.back() != " ")
		dictionary.push_back(" ");
	return dictionary;
}

class OnnxOcrEngine final : public OcrEngine
{
public:
	bool load(const OcrConfig &config, std::string *error) override
	{
		ready_ = false;
		det_session_.reset();
		rec_session_.reset();
		config_ = config;

		std::ifstream det_file(config.detection_model);
		if (!det_file)
		{
			if (error)
				*error = "detection model not found: " + config.detection_model;
			return false;
		}
		std::ifstream rec_file(config.recognition_model);
		if (!rec_file)
		{
			if (error)
				*error = "recognition model not found: " + config.recognition_model;
			return false;
		}

		std::vector<std::string> dictionary = ReadDictionary(config.dictionary, error);
		if (dictionary.empty())
			return false;
		decoder_.setDictionary(std::move(dictionary));

		try
		{
			env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "chiaki-translator");
			Ort::SessionOptions options;
			options.SetIntraOpNumThreads(std::max(1, config.threads));
			options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
			options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
			det_session_ = std::make_unique<Ort::Session>(*env_, config.detection_model.c_str(), options);
			rec_session_ = std::make_unique<Ort::Session>(*env_, config.recognition_model.c_str(), options);
			ReadNames(*det_session_, det_input_names_, det_output_names_);
			ReadNames(*rec_session_, rec_input_names_, rec_output_names_);
			if (det_input_names_.empty() || rec_input_names_.empty())
			{
				if (error)
					*error = "OCR model has no inputs";
				return false;
			}
		}
		catch (const std::exception &exception)
		{
			if (error)
				*error = std::string("failed to initialize ONNX Runtime: ") + exception.what();
			det_session_.reset();
			rec_session_.reset();
			return false;
		}

		if (error)
			error->clear();
		ready_ = true;
		return true;
	}

	std::vector<OcrLine> recognize(const Image &image, std::string *error) override
	{
		std::vector<OcrLine> lines;
		if (!ready_ || !det_session_ || !rec_session_)
		{
			if (error)
				*error = "OCR engine is not loaded";
			return lines;
		}
		if (image.empty())
		{
			if (error)
				*error = "OCR input image is empty";
			return lines;
		}

		try
		{
			const DetectionTensor detection = PrepareDetection(image, config_.detection_limit_side);
			if (detection.width <= 0 || detection.height <= 0)
				return lines;

			const std::array<int64_t, 4> det_shape{1, 3, detection.height, detection.width};
			Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
			Ort::Value det_input = Ort::Value::CreateTensor<float>(memory,
				const_cast<float *>(detection.data.data()), detection.data.size(),
				det_shape.data(), det_shape.size());

			const char *det_input_name = det_input_names_.front().c_str();
			const char *det_output_name = det_output_names_.front().c_str();
			std::vector<Ort::Value> det_outputs = det_session_->Run(Ort::RunOptions{nullptr},
				&det_input_name, &det_input, 1, &det_output_name, 1);
			if (det_outputs.empty())
				return lines;

			const auto det_info = det_outputs[0].GetTensorTypeAndShapeInfo();
			const std::vector<int64_t> det_output_shape = det_info.GetShape();
			if (det_output_shape.size() != 4)
			{
				if (error)
					*error = "unexpected detection output shape";
				return lines;
			}
			const int map_height = static_cast<int>(det_output_shape[2]);
			const int map_width = static_cast<int>(det_output_shape[3]);
			const float *probability = det_outputs[0].GetTensorData<float>();

			DbConfig db_config;
			db_config.threshold = config_.detection_threshold;
			db_config.box_threshold = config_.box_threshold;
			db_config.unclip_ratio = config_.unclip_ratio;
			db_config.min_size = 3;
			const std::vector<DetBox> boxes = dbPostProcess(probability, map_width, map_height,
				db_config, image.width, image.height);

			auto recognize_crop = [&](const Image &crop, float *confidence) -> std::string {
				int recognition_width = 0;
				const std::vector<float> input = PrepareRecognition(crop, config_.recognition_height, recognition_width);
				if (input.empty() || recognition_width <= 0)
					return {};
				const std::array<int64_t, 4> rec_shape{1, 3, config_.recognition_height, recognition_width};
				Ort::Value rec_input = Ort::Value::CreateTensor<float>(memory,
					const_cast<float *>(input.data()), input.size(), rec_shape.data(), rec_shape.size());
				const char *rec_input_name = rec_input_names_.front().c_str();
				const char *rec_output_name = rec_output_names_.front().c_str();
				std::vector<Ort::Value> rec_outputs = rec_session_->Run(Ort::RunOptions{nullptr},
					&rec_input_name, &rec_input, 1, &rec_output_name, 1);
				if (rec_outputs.empty())
					return {};
				const auto rec_info = rec_outputs[0].GetTensorTypeAndShapeInfo();
				const std::vector<int64_t> rec_output_shape = rec_info.GetShape();
				if (rec_output_shape.size() != 3)
					return {};
				CtcResult decoded = decoder_.decode(rec_outputs[0].GetTensorData<float>(),
					static_cast<int>(rec_output_shape[1]), static_cast<int>(rec_output_shape[2]));
				while (!decoded.text.empty() && decoded.text.back() == ' ')
					decoded.text.pop_back();
				*confidence = decoded.confidence;
				return std::move(decoded.text);
			};

			for (const DetBox &detected : boxes)
			{
				const Image crop = cropImage(image, detected.box);
				if (crop.empty())
					continue;

				float confidence = 0.0f;
				std::string text = recognize_crop(crop, &confidence);
				int angle = 0;

				// Tall crops may be sideways text or upright vertical Japanese.
				// Try the rotated variant too and keep the more confident reading.
				if (config_.try_vertical && crop.height * 2 >= crop.width * 3)
				{
					float rotated_confidence = 0.0f;
					std::string rotated = recognize_crop(rotate90CounterClockwise(crop), &rotated_confidence);
					if (!rotated.empty() && rotated_confidence > confidence)
					{
						text = std::move(rotated);
						confidence = rotated_confidence;
						angle = 90;
					}
				}

				if (text.empty() || confidence < 0.25f)
					continue;

				OcrLine line;
				line.text = std::move(text);
				line.confidence = confidence;
				line.box = detected.box;
				line.angle = angle;
				lines.push_back(std::move(line));
			}

			if (config_.merge_vertical)
				lines = mergeVerticalLines(lines);
		}
		catch (const std::exception &exception)
		{
			if (error)
				*error = std::string("OCR inference failed: ") + exception.what();
			return {};
		}

		if (error)
			error->clear();
		return lines;
	}

	const char *name() const override { return "paddle-onnx"; }
	bool ready() const override { return ready_; }

private:
	static void ReadNames(Ort::Session &session, std::vector<std::string> &inputs,
			std::vector<std::string> &outputs)
	{
		Ort::AllocatorWithDefaultOptions allocator;
		inputs.clear();
		outputs.clear();
		for (size_t index = 0; index < session.GetInputCount(); ++index)
			inputs.emplace_back(session.GetInputNameAllocated(index, allocator).get());
		for (size_t index = 0; index < session.GetOutputCount(); ++index)
			outputs.emplace_back(session.GetOutputNameAllocated(index, allocator).get());
	}

	std::unique_ptr<Ort::Env> env_;
	std::unique_ptr<Ort::Session> det_session_;
	std::unique_ptr<Ort::Session> rec_session_;
	std::vector<std::string> det_input_names_;
	std::vector<std::string> det_output_names_;
	std::vector<std::string> rec_input_names_;
	std::vector<std::string> rec_output_names_;
	CtcDecoder decoder_;
	OcrConfig config_;
	bool ready_ = false;
};

} // namespace

std::unique_ptr<OcrEngine> createOnnxOcrEngine()
{
	return std::make_unique<OnnxOcrEngine>();
}

} // namespace trans

#else // TRANS_HAVE_ONNXRUNTIME

namespace trans {

std::unique_ptr<OcrEngine> createOnnxOcrEngine()
{
	return nullptr;
}

} // namespace trans

#endif
