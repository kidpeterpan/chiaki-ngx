// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/OcrEngine.hpp>

namespace trans {
namespace {

class NullOcrEngine final : public OcrEngine
{
public:
	bool load(const OcrConfig &, std::string *error) override
	{
		if (error)
			*error = "OCR support is not available: rebuild with ONNX Runtime (TRANS_ENABLE_ONNXRUNTIME=ON)";
		return false;
	}

	std::vector<OcrLine> recognize(const Image &, std::string *error) override
	{
		if (error)
			*error = "OCR support is not available in this build";
		return {};
	}

	const char *name() const override { return "null"; }
	bool ready() const override { return false; }
};

} // namespace

std::unique_ptr<OcrEngine> createNullOcrEngine()
{
	return std::make_unique<NullOcrEngine>();
}

} // namespace trans
