// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// OCR engine abstraction. The only implementation here wraps ONNX Runtime;
// builds without it get a null engine so the rest of the pipeline still runs.
#pragma once

#include <trans/Types.hpp>

#include <memory>
#include <string>
#include <vector>

namespace trans {

struct OcrConfig
{
	std::string detection_model;   // paddle det .onnx
	std::string recognition_model; // paddle rec .onnx
	std::string dictionary;        // ppocr_keys_v1.txt compatible
	int threads = 2;               // worker budget on the 8-thread Zen 2
	int detection_limit_side = 960;
	int recognition_height = 48;
	float detection_threshold = 0.3f;
	float box_threshold = 0.5f;
	float unclip_ratio = 1.6f;
	bool try_vertical = true;
	bool merge_vertical = true;
	// Cloud OCR options (Google Cloud Vision / OCR.space). On-device engines
	// ignore these fields.
	std::string cloud_provider;
	std::string cloud_api_key;
	std::string cloud_endpoint;
	std::string cloud_language = "jpn";
	int cloud_timeout_ms = 20000;
};

class OcrEngine
{
public:
	virtual ~OcrEngine() = default;
	virtual bool load(const OcrConfig &config, std::string *error) = 0;
	virtual std::vector<OcrLine> recognize(const Image &image, std::string *error) = 0;
	virtual const char *name() const = 0;
	virtual bool ready() const = 0;
};

std::unique_ptr<OcrEngine> createNullOcrEngine();

// Returns nullptr when this build has no ONNX Runtime support.
std::unique_ptr<OcrEngine> createOnnxOcrEngine();

} // namespace trans
