// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Cloud OCR adapters for the Decky-Translator provider set: Google Cloud
// Vision and OCR.space. This file is Qt-facing; translator_core stays Qt-free.
#pragma once

#include <trans/OcrEngine.hpp>

#include <string>

class CloudOcrEngine final : public trans::OcrEngine
{
public:
	bool load(const trans::OcrConfig &config, std::string *error) override;
	std::vector<trans::OcrLine> recognize(const trans::Image &image, std::string *error) override;
	const char *name() const override { return name_.c_str(); }
	bool ready() const override { return ready_; }

private:
	std::string name_ = "cloud-ocr";
	std::string provider_;
	std::string api_key_;
	std::string endpoint_;
	std::string language_ = "jpn";
	int timeout_ms_ = 20000;
	bool ready_ = false;
};
