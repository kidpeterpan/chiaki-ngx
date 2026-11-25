// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Optional Chrome Screen AI OCR backend. The engine is the Chromium Screen AI
// shared library and its TFLite resources, fetched separately; the AppImage
// build bundles them on Linux x86_64.
#pragma once

#include <trans/OcrEngine.hpp>

#include <string>

class ScreenAiEngine final : public trans::OcrEngine
{
public:
	~ScreenAiEngine() override;
	bool load(const trans::OcrConfig &config, std::string *error) override;
	std::vector<trans::OcrLine> recognize(const trans::Image &image, std::string *error) override;
	const char *name() const override { return "chromescreenai"; }
	bool ready() const override { return ready_; }

private:
	void unload();

	void *library_ = nullptr;
	void *perform_ = nullptr;
	void *free_ = nullptr;
	std::string model_dir_;
	unsigned max_dimension_ = 2048;
	bool ready_ = false;
};
