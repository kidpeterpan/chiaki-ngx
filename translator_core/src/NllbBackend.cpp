// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include <trans/NllbBackend.hpp>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#if defined(TRANS_HAVE_CTRANSLATE2)

#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

#include <chrono>
#include <stdexcept>

namespace trans {
namespace {

constexpr const char *kEndToken = "</s>";

class NllbBackend final : public TranslatorBackend
{
public:
	explicit NllbBackend(NllbConfig config) : config_(std::move(config)) {}
	~NllbBackend() override { stop(); }

	bool load(std::string *error)
	{
		try
		{
			tokenizer_ = std::make_unique<sentencepiece::SentencePieceProcessor>();
			const auto status = tokenizer_->Load(config_.sentencepiece_model);
			if (!status.ok())
			{
				if (error)
					*error = "cannot load SentencePiece model";
				return false;
			}

			ctranslate2::ReplicaPoolConfig pool_config;
			pool_config.num_threads_per_replica = std::max(1, config_.intra_threads);
			translator_ = std::make_unique<ctranslate2::Translator>(
				config_.model_dir, ctranslate2::Device::CPU, ctranslate2::ComputeType::INT8,
				std::vector<int>{0}, false, pool_config);
		}
		catch (const std::exception &exception)
		{
			if (error)
				*error = std::string("cannot load NLLB model: ") + exception.what();
			translator_.reset();
			tokenizer_.reset();
			return false;
		}
		if (error)
			error->clear();
		running_.store(true);
		worker_ = std::thread(&NllbBackend::run, this);
		return true;
	}

	void translate(const TranslationRequest &request, TranslationCallback callback) override
	{
		if (!running_.load())
		{
			if (callback)
			{
				TranslationResult result;
				result.ok = false;
				result.error = "NLLB backend is not running";
				callback(std::move(result));
			}
			return;
		}
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (tasks_.size() >= 8)
			{
				if (callback)
				{
					TranslationResult result;
					result.ok = false;
					result.error = "NLLB translation queue is full";
					callback(std::move(result));
				}
				return;
			}
			tasks_.push_back(Task{request, std::move(callback)});
		}
		condition_.notify_one();
	}

	const char *name() const override { return "nllb-ct2"; }
	bool offline() const override { return true; }

private:
	struct Task
	{
		TranslationRequest request;
		TranslationCallback callback;
	};

	void stop()
	{
		if (!running_.exchange(false))
			return;
		condition_.notify_all();
		if (worker_.joinable())
			worker_.join();
	}

	void run()
	{
		while (running_.load())
		{
			Task task;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				condition_.wait(lock, [this]() { return !running_.load() || !tasks_.empty(); });
				if (!running_.load())
					break;
				task = std::move(tasks_.front());
				tasks_.pop_front();
			}

			TranslationResult result;
			const auto begin = std::chrono::steady_clock::now();
			try
			{
				result.text = translateText(task.request.text);
				result.ok = !result.text.empty();
				if (!result.ok)
					result.error = "NLLB returned an empty translation";
			}
			catch (const std::exception &exception)
			{
				result.ok = false;
				result.error = std::string("NLLB translation failed: ") + exception.what();
			}
			result.latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - begin).count();
			if (task.callback)
				task.callback(std::move(result));
		}
	}

	std::string translateText(const std::string &text)
	{
		if (text.empty())
			return {};
		std::vector<std::string> pieces;
		const auto status = tokenizer_->Encode(text, &pieces);
		if (!status.ok())
			throw std::runtime_error("SentencePiece encode failed");

		std::vector<std::string> source;
		source.reserve(pieces.size() + 2);
		source.push_back(config_.source_language);
		source.insert(source.end(), pieces.begin(), pieces.end());
		source.push_back(kEndToken);

		std::vector<std::vector<std::string>> batch{source};
		std::vector<std::vector<std::string>> target_prefix{{config_.target_language}};

		ctranslate2::TranslationOptions options;
		options.beam_size = static_cast<size_t>(std::max(1, config_.beam_size));
		options.max_decoding_length = static_cast<size_t>(std::max(8, config_.max_decoding_length));
		const std::vector<ctranslate2::TranslationResult> results =
			translator_->translate_batch(batch, target_prefix, options);
		if (results.empty() || results.front().hypotheses.empty())
			throw std::runtime_error("empty NLLB result");

		std::vector<std::string> hypothesis = results.front().hypotheses.front();
		if (!hypothesis.empty() && hypothesis.front() == config_.target_language)
			hypothesis.erase(hypothesis.begin());

		std::string decoded;
		const auto decode_status = tokenizer_->Decode(hypothesis, &decoded);
		if (!decode_status.ok())
			throw std::runtime_error("SentencePiece decode failed");
		while (!decoded.empty() && (decoded.back() == ' ' || decoded.back() == '\n'))
			decoded.pop_back();
		while (!decoded.empty() && decoded.front() == ' ')
			decoded.erase(decoded.begin());
		return decoded;
	}

	NllbConfig config_;
	std::unique_ptr<ctranslate2::Translator> translator_;
	std::unique_ptr<sentencepiece::SentencePieceProcessor> tokenizer_;
	std::thread worker_;
	std::mutex mutex_;
	std::condition_variable condition_;
	std::deque<Task> tasks_;
	std::atomic<bool> running_{false};
};

} // namespace

std::shared_ptr<TranslatorBackend> createNllbBackend(const NllbConfig &config, std::string *error)
{
	if (config.model_dir.empty() || config.sentencepiece_model.empty())
	{
		if (error)
			*error = "NLLB model directory or SentencePiece model is not configured";
		return nullptr;
	}
	auto backend = std::make_shared<NllbBackend>(config);
	if (!backend->load(error))
		return nullptr;
	return backend;
}

} // namespace trans

#else

namespace trans {

std::shared_ptr<TranslatorBackend> createNllbBackend(const NllbConfig &, std::string *error)
{
	if (error)
		*error = "offline NLLB is not available: rebuild with TRANS_ENABLE_CTRANSLATE2=ON";
	return nullptr;
}

} // namespace trans

#endif
