// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Offline NLLB-200 translation through CTranslate2 and SentencePiece.
#pragma once

#include <trans/TranslatorBackend.hpp>

#include <memory>
#include <string>

namespace trans {

struct NllbConfig
{
	std::string model_dir;              // CTranslate2 NLLB model directory
	std::string sentencepiece_model;   // sentencepiece.bpe.model
	std::string source_language = "jpn_Jpan";
	std::string target_language = "tha_Thai";
	int intra_threads = 2;
	int beam_size = 2;
	int max_decoding_length = 256;
};

// Returns nullptr when the build has no TRANS_ENABLE_CTRANSLATE2 support or
// when the model cannot be loaded.
std::shared_ptr<TranslatorBackend> createNllbBackend(const NllbConfig &config, std::string *error);

} // namespace trans
