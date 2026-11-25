// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/translatormanager.h"

#include "translator/cloudbackends.h"
#include "translator/cloudocr.h"
#include "translator/screenai.h"
#include "translator/shortcuts.h"

#include <trans/DictLookup.hpp>
#include <trans/NllbBackend.hpp>
#include <trans/OcrEngine.hpp>
#include <trans/TranslatorPipeline.hpp>
#include <trans/Types.hpp>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QUrl>
#include <qqml.h>

#include "controllermanager.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include <algorithm>
#include <cmath>

namespace {

constexpr int64_t kTapIntervalMs = 100;
constexpr int kBacklogLimit = 50;

bool ToTranslatorFormat(int format, trans::PixelFormat &out)
{
	switch (format)
	{
	case AV_PIX_FMT_NV12:
		out = trans::PixelFormat::NV12;
		return true;
	case AV_PIX_FMT_YUV420P:
		out = trans::PixelFormat::I420;
		return true;
	case AV_PIX_FMT_P010LE:
		out = trans::PixelFormat::P010;
		return true;
	case AV_PIX_FMT_RGBA:
		out = trans::PixelFormat::RGBA;
		return true;
	case AV_PIX_FMT_BGRA:
		out = trans::PixelFormat::BGRA;
		return true;
	case AV_PIX_FMT_GRAY8:
		out = trans::PixelFormat::GRAY8;
		return true;
	default:
		return false;
	}
}

trans::OcrPreprocess ParsePreprocess(const QString &value)
{
	if (value == QStringLiteral("threshold"))
		return trans::OcrPreprocess::Threshold;
	if (value == QStringLiteral("none"))
		return trans::OcrPreprocess::None;
	return trans::OcrPreprocess::Contrast;
}

} // namespace

TranslatorManager::TranslatorManager(QObject *parent) : QObject(parent)
{
	qmlRegisterSingletonInstance("org.streetpea.chiaking", 1, 0, "Translator", this);

	QSettings settings;
	enabled_.store(settings.value(QStringLiteral("translator/enabled"), false).toBool());
	overlay_visible_ = settings.value(QStringLiteral("translator/overlay_visible"), true).toBool();
	backend_id_ = settings.value(QStringLiteral("translator/backend"), QStringLiteral("gemini")).toString();
	ocr_provider_ = settings.value(QStringLiteral("translator/ocr"), QStringLiteral("paddle")).toString();
	if (!ocrProviderIds().contains(ocr_provider_))
		ocr_provider_ = QStringLiteral("paddle");
	ocr_api_key_ = settings.value(QStringLiteral("translator/ocr_api_key")).toString();
	auto load_shortcut = [&settings](const char *key, const QString &fallback) {
		const QString stored = settings.value(QString::fromLatin1(key), fallback).toString();
		if (stored.trimmed().isEmpty())
			return QString();
		const QString normalized = translator::normalizeShortcut(stored);
		return normalized.isEmpty() ? fallback : normalized;
	};
	shortcut_translate_ = load_shortcut("translator/shortcut/translate", QStringLiteral("F13"));
	shortcut_overlay_ = load_shortcut("translator/shortcut/overlay", QStringLiteral("F14"));
	shortcut_backlog_ = load_shortcut("translator/shortcut/backlog", QStringLiteral("F15"));
	shortcut_settings_ = load_shortcut("translator/shortcut/settings", QStringLiteral("F12"));
	target_language_ = settings.value(QStringLiteral("translator/target"), QStringLiteral("th")).toString();
	preprocess_ = settings.value(QStringLiteral("translator/preprocess"), QStringLiteral("contrast")).toString();
	nllb_model_path_ = settings.value(QStringLiteral("translator/nllb_model"),
		qEnvironmentVariable("CHIAKI_TRANSLATOR_NLLB_MODEL")).toString();
	preset_name_ = settings.value(QStringLiteral("translator/preset"), QStringLiteral("default")).toString();
	if (preset_name_.isEmpty())
		preset_name_ = QStringLiteral("default");

	roi_x_ = settings.value(QStringLiteral("translator/preset/") + preset_name_ + QStringLiteral("/x"), roi_x_).toDouble();
	roi_y_ = settings.value(QStringLiteral("translator/preset/") + preset_name_ + QStringLiteral("/y"), roi_y_).toDouble();
	roi_w_ = settings.value(QStringLiteral("translator/preset/") + preset_name_ + QStringLiteral("/w"), roi_w_).toDouble();
	roi_h_ = settings.value(QStringLiteral("translator/preset/") + preset_name_ + QStringLiteral("/h"), roi_h_).toDouble();

	ocr_ = trans::createOnnxOcrEngine();
	dictionary_ = std::make_unique<trans::JmDict>();
	pipeline_ = std::make_unique<trans::TranslatorPipeline>();

	connect(ControllerManager::GetInstance(), &ControllerManager::PaddleButton, this,
		[this](int paddle, bool pressed) {
			if (!pressed)
				return;
			switch (paddle)
			{
			case 1: // L4: translate now
				manualTranslate();
				break;
			case 2: // R4: show/hide translated subtitles
				toggleOverlay();
				break;
			case 3: // L5: dialogue backlog
				toggleBacklog();
				break;
			case 4: // R5: adjust the ROI
				setRoiSelectionActive(!roiSelectionActive());
				break;
			default:
				break;
			}
		});

	if (!ocr_)
		status_text_ = QStringLiteral("OCR backend unavailable: rebuild with ONNX Runtime");
	else
		status_text_ = QStringLiteral("Translator idle");
}

TranslatorManager::~TranslatorManager()
{
	alive_.store(false);
	{
		std::lock_guard<std::mutex> lock(pipeline_mutex_);
		if (pipeline_)
			pipeline_->stop();
	}
	cloud_backend_.reset();
	backend_.reset();
	dictionary_.reset();
}

QString TranslatorManager::settingsKey(const QString &suffix) const
{
	return QStringLiteral("translator/") + suffix;
}

QString TranslatorManager::assetPath(const QString &relative) const
{
	QStringList roots;
	const QByteArray environment = qgetenv("CHIAKI_TRANSLATOR_ASSETS");
	if (!environment.isEmpty())
		roots << QString::fromLocal8Bit(environment);
	const QString app_dir = QCoreApplication::applicationDirPath();
	roots << app_dir + QStringLiteral("/translator");
	roots << app_dir + QStringLiteral("/../share/chiaki-ngx/translator");
	// Keep the pre-rename install location working for packaged builds.
	roots << app_dir + QStringLiteral("/../share/chiaki-ng/translator");
	roots << app_dir + QStringLiteral("/../Resources/translator");
	roots << QDir::currentPath() + QStringLiteral("/assets");
	roots << QDir::currentPath() + QStringLiteral("/translator_core/assets");

	for (const QString &root : roots)
	{
		const QString candidate = QDir(root).filePath(relative);
		if (QFileInfo::exists(candidate))
			return candidate;
	}
	return {};
}

qreal TranslatorManager::clampUnit(qreal value) const
{
	return std::clamp(value, 0.0, 1.0);
}

trans::Rect TranslatorManager::pixelRoi(int width, int height) const
{
	if (width <= 0 || height <= 0)
		return {};
	const int x = static_cast<int>(std::lround(roi_x_ * width));
	const int y = static_cast<int>(std::lround(roi_y_ * height));
	const int w = std::max(8, static_cast<int>(std::lround(roi_w_ * width)));
	const int h = std::max(8, static_cast<int>(std::lround(roi_h_ * height)));
	return trans::Rect{std::max(0, std::min(x, width - 8)), std::max(0, std::min(y, height - 8)),
		std::min(w, width), std::min(h, height)};
}

QVariantMap TranslatorManager::roiNormalized() const
{
	std::lock_guard<std::mutex> lock(roi_mutex_);
	QVariantMap map;
	map.insert(QStringLiteral("x"), roi_x_);
	map.insert(QStringLiteral("y"), roi_y_);
	map.insert(QStringLiteral("width"), roi_w_);
	map.insert(QStringLiteral("height"), roi_h_);
	return map;
}

void TranslatorManager::saveRoi()
{
	QSettings settings;
	const QString prefix = QStringLiteral("translator/preset/") + preset_name_;
	settings.setValue(prefix + QStringLiteral("/x"), roi_x_);
	settings.setValue(prefix + QStringLiteral("/y"), roi_y_);
	settings.setValue(prefix + QStringLiteral("/w"), roi_w_);
	settings.setValue(prefix + QStringLiteral("/h"), roi_h_);
	settings.setValue(QStringLiteral("translator/preset"), preset_name_);
	QStringList names = settings.value(QStringLiteral("translator/preset_names")).toStringList();
	if (!names.contains(preset_name_))
		names.append(preset_name_);
	settings.setValue(QStringLiteral("translator/preset_names"), names);
	settings.sync();
}

void TranslatorManager::setEnabled(bool enabled)
{
	if (enabled_.load() == enabled)
		return;
	enabled_.store(enabled);
	QSettings settings;
	settings.setValue(QStringLiteral("translator/enabled"), enabled);
	if (enabled)
		prepare();
	else
		shutdown();
	emit enabledChanged();
}

void TranslatorManager::setOverlayVisible(bool visible)
{
	if (overlay_visible_ == visible)
		return;
	overlay_visible_ = visible;
	QSettings().setValue(QStringLiteral("translator/overlay_visible"), visible);
	emit overlayVisibleChanged();
}

void TranslatorManager::setBacklogVisible(bool visible)
{
	if (backlog_visible_ == visible)
		return;
	backlog_visible_ = visible;
	emit backlogVisibleChanged();
}

void TranslatorManager::setRoiSelectionActive(bool active)
{
	if (roi_selection_active_.load() == active)
		return;
	roi_selection_active_.store(active);
	emit roiSelectionActiveChanged();
}

void TranslatorManager::setBackendId(const QString &backend_id)
{
	if (backend_id_ == backend_id)
		return;
	backend_id_ = backend_id;
	QSettings().setValue(QStringLiteral("translator/backend"), backend_id_);
	emit backendIdChanged();
	if (prepared_.load())
		restartPipeline();
}

void TranslatorManager::setOcrProvider(const QString &provider)
{
	if (!ocrProviderIds().contains(provider) || ocr_provider_ == provider)
		return;
	ocr_provider_ = provider;
	QSettings().setValue(QStringLiteral("translator/ocr"), ocr_provider_);
	emit ocrProviderChanged();
	if (prepared_.load())
		restartPipeline();
}

void TranslatorManager::setOcrApiKey(const QString &key)
{
	if (ocr_api_key_ == key)
		return;
	ocr_api_key_ = key;
	QSettings().setValue(QStringLiteral("translator/ocr_api_key"), ocr_api_key_);
	emit ocrApiKeyChanged();
	if (prepared_.load())
		restartPipeline();
}

bool TranslatorManager::setShortcuts(const QString &translate, const QString &overlay,
	const QString &backlog, const QString &settings)
{
	const QString inputs[4] = {translate, overlay, backlog, settings};
	const char *keys[4] = {"translator/shortcut/translate", "translator/shortcut/overlay",
		"translator/shortcut/backlog", "translator/shortcut/settings"};
	const QStringList values = {translate, overlay, backlog, settings};
	const QString problem = translator::shortcutSetError(values);
	if (!problem.isEmpty())
	{
		setStatus(problem);
		return false;
	}
	QString normalized[4];
	for (int index = 0; index < 4; ++index)
		normalized[index] = translator::normalizeShortcut(inputs[index]);
	shortcut_translate_ = normalized[0];
	shortcut_overlay_ = normalized[1];
	shortcut_backlog_ = normalized[2];
	shortcut_settings_ = normalized[3];
	QSettings settings_store;
	for (int index = 0; index < 4; ++index)
		settings_store.setValue(QString::fromLatin1(keys[index]), normalized[index]);
	emit shortcutsChanged();
	setStatus(QStringLiteral("Hotkeys saved"));
	return true;
}

void TranslatorManager::resetShortcuts()
{
	setShortcuts(QStringLiteral("F13"), QStringLiteral("F14"), QStringLiteral("F15"), QStringLiteral("F12"));
}

void TranslatorManager::showSettings()
{
	emit settingsRequested();
}

void TranslatorManager::setTargetLanguage(const QString &language)
{
	if (target_language_ == language)
		return;
	target_language_ = language;
	QSettings().setValue(QStringLiteral("translator/target"), target_language_);
	emit targetLanguageChanged();
	if (prepared_.load())
		restartPipeline();
}

void TranslatorManager::setStatus(const QString &status)
{
	if (status_text_ == status)
		return;
	status_text_ = status;
	emit statusTextChanged();
}

void TranslatorManager::ensureDictionary()
{
	if (dictionary_ready_ || !dictionary_)
		return;
	const QString path = assetPath(QStringLiteral("dict/jmdict.db"));
	if (path.isEmpty())
	{
		setStatus(QStringLiteral("JMdict database missing (assets/dict/jmdict.db)"));
		return;
	}
	std::string error;
	if (!dictionary_->open(path.toStdString(), &error))
	{
		setStatus(QStringLiteral("Failed to open JMdict: %1").arg(QString::fromStdString(error)));
		return;
	}
	dictionary_ready_ = true;
	emit dictionaryReadyChanged();
}

bool TranslatorManager::ensureBackend()
{
	cloud_backend_.reset();
	backend_.reset();

	if (ocr_provider_ == QStringLiteral("gemini-vision"))
	{
		// The OCR call already returns the translation; no second backend is needed.
		backend_ = std::make_shared<trans::NullTranslatorBackend>();
		setStatus(QStringLiteral("Ready (Gemini Vision OCR + translation)"));
		return true;
	}

	if (backend_id_ == QStringLiteral("offline"))
	{
		const QString model_dir = nllb_model_path_.trimmed();
		if (model_dir.isEmpty())
		{
			backend_ = std::make_shared<trans::NullTranslatorBackend>();
			setStatus(QStringLiteral("Set the offline NLLB model directory in translator settings"));
			return false;
		}
		trans::NllbConfig nllb;
		nllb.model_dir = model_dir.toStdString();
		nllb.sentencepiece_model = QDir(model_dir)
			.filePath(QStringLiteral("sentencepiece.bpe.model")).toStdString();
		nllb.source_language = "jpn_Jpan";
		nllb.target_language = target_language_ == QStringLiteral("en")
			? "eng_Latn"
			: (target_language_ == QStringLiteral("ja") ? "jpn_Jpan" : "tha_Thai");
		nllb.intra_threads = 2;
		nllb.beam_size = 2;
		std::string nllb_error;
		backend_ = trans::createNllbBackend(nllb, &nllb_error);
		if (!backend_)
		{
			backend_ = std::make_shared<trans::NullTranslatorBackend>();
			setStatus(QStringLiteral("Offline NLLB failed to load: %1")
				.arg(QString::fromStdString(nllb_error)));
			return false;
		}
		setStatus(QStringLiteral("Ready (offline NLLB)"));
		return true;
	}

	if (backend_id_ == QStringLiteral("opencode-go") || backend_id_ == QStringLiteral("openai"))
	{
		CloudBackendOptions openai_options;
		openai_options.provider = backend_id_;
		openai_options.api_key = apiKey(backend_id_);
		openai_options.model = model(backend_id_);
		const QString base_url = baseUrl(backend_id_);
		if (!base_url.isEmpty())
			openai_options.openai_base_url = QUrl(base_url);
		const QString problem = CloudTranslatorBackend::validateOptions(openai_options);
		if (!problem.isEmpty())
		{
			backend_ = std::make_shared<trans::NullTranslatorBackend>();
			setStatus(problem);
			return false;
		}
		cloud_backend_ = std::make_shared<CloudTranslatorBackend>(std::move(openai_options));
		backend_ = cloud_backend_;
		setStatus(QStringLiteral("Ready (%1)").arg(backend_id_));
		return true;
	}

	const QString key = apiKey(backend_id_);
	if (key.isEmpty() && backend_id_ != QStringLiteral("google"))
	{
		backend_ = std::make_shared<trans::NullTranslatorBackend>();
		setStatus(QStringLiteral("Add a %1 API key in translator settings").arg(backend_id_));
		return false;
	}

	QSettings settings;
	const QString model = settings.value(QStringLiteral("translator/gemini_model"),
		QStringLiteral("gemini-2.5-flash")).toString();
	CloudBackendOptions options;
	options.provider = backend_id_;
	options.api_key = key;
	options.model = model;
	cloud_backend_ = std::make_shared<CloudTranslatorBackend>(std::move(options));
	backend_ = cloud_backend_;
	setStatus(QStringLiteral("Ready (%1)").arg(backend_id_));
	return true;
}

bool TranslatorManager::ensurePipeline(const trans::Rect &roi, QString *error)
{
	if (!pipeline_)
	{
		if (error)
			*error = QStringLiteral("pipeline is not initialized");
		return false;
	}
	if (pipeline_->running())
		return true;
	if (!backend_)
	{
		if (error)
			*error = QStringLiteral("no translation backend configured");
		return false;
	}

	const bool cloud_ocr = ocr_provider_ == QStringLiteral("google-vision") ||
		ocr_provider_ == QStringLiteral("gemini-vision") ||
		ocr_provider_ == QStringLiteral("ocrspace");
	const bool screen_ai = ocr_provider_ == QStringLiteral("chromescreenai");
	if (cloud_ocr)
		ocr_ = std::make_shared<CloudOcrEngine>();
	else if (screen_ai)
		ocr_ = std::make_shared<ScreenAiEngine>();
	else
		ocr_ = trans::createOnnxOcrEngine();
	if (!ocr_)
	{
		if (error)
			*error = QStringLiteral("OCR backend not available; rebuild with ONNX Runtime");
		return false;
	}

	QString det;
	QString rec;
	QString dict;
	if (!cloud_ocr && !screen_ai)
	{
		if (ocr_provider_ == QStringLiteral("rapidocr"))
		{
			det = assetPath(QStringLiteral("models/rapidocr/ch_PP-OCRv5_mobile_det.onnx"));
			rec = assetPath(QStringLiteral("models/rapidocr/ch_rec.onnx"));
			dict = assetPath(QStringLiteral("models/rapidocr/ch_dict.txt"));
		}
		else
		{
			det = assetPath(QStringLiteral("models/ch_PP-OCRv3_det.onnx"));
			rec = assetPath(QStringLiteral("models/japan_PP-OCRv3_rec.onnx"));
			dict = assetPath(QStringLiteral("models/japan_dict.txt"));
		}
		if (det.isEmpty() || rec.isEmpty() || dict.isEmpty())
		{
			if (error)
				*error = QStringLiteral("OCR model files for %1 not found; run scripts/translator/fetch-assets.sh")
					.arg(ocr_provider_);
			return false;
		}
	}

	trans::PipelineConfig config;
	config.frame.roi = roi;
	config.frame.sample_interval_ms = 100;
	config.frame.stable_ms = 250;
	config.frame.still_changed_ratio = 0.002;
	config.frame.still_mean_abs_delta = 1.0;
	config.frame.pixel_delta_threshold = 14;
	config.frame.min_stddev = 2.0;
	config.frame.diff_max_width = 640;
	if (cloud_ocr)
	{
		config.ocr.cloud_provider = ocr_provider_.toStdString();
		config.ocr.cloud_api_key = ocr_api_key_.toStdString();
		config.ocr.cloud_language = "jpn";
		config.ocr.cloud_target_language = target_language_.toStdString();
	}
	else if (screen_ai)
	{
		const QString assets = assetPath(QStringLiteral("models/screenai/resources"));
		if (assets.isEmpty())
		{
			if (error)
				*error = QStringLiteral("Chrome Screen AI assets missing; run scripts/translator/fetch-screenai.sh");
			return false;
		}
		config.ocr.screenai_model_dir = assets.toStdString();
	}
	else
	{
		config.ocr.detection_model = det.toStdString();
		config.ocr.recognition_model = rec.toStdString();
		config.ocr.dictionary = dict.toStdString();
	}
	config.ocr.threads = 2;
	config.ocr.detection_limit_side = 960;
	config.ocr.try_vertical = true;
	config.ocr.merge_vertical = true;
	config.prompt.source_language = QStringLiteral("Japanese").toStdString();
	config.prompt.target_language = target_language_.toStdString();
	config.preprocess = ParsePreprocess(preprocess_);
	config.context_pairs = 3;
	config.backlog_limit = kBacklogLimit;

	const QString affinity = QSettings().value(QStringLiteral("translator/affinity"),
#ifdef Q_OS_LINUX
		QStringLiteral("2,3")
#else
		QString()
#endif
	).toString();
	for (const QString &core : affinity.split(',', Qt::SkipEmptyParts))
	{
		bool ok = false;
		const int value = core.trimmed().toInt(&ok);
		if (ok)
			config.worker_cpu_affinity.push_back(value);
	}

	trans::PipelineCallbacks callbacks;
	callbacks.onOcr = [this](const std::string &text, float confidence) {
		if (!alive_.load())
			return;
		const QString source = QString::fromStdString(text);
		QMetaObject::invokeMethod(this, [this, source, confidence]() {
			source_text_ = source;
			emit sourceTextChanged();
			setStatus(QStringLiteral("OCR %1%").arg(QString::number(confidence * 100.0f, 'f', 0)));
		}, Qt::QueuedConnection);
	};
	callbacks.onTranslation = [this](const trans::TranslationItem &item) {
		if (!alive_.load())
			return;
		QMetaObject::invokeMethod(this, [this, item]() {
			processTranslation(item);
		}, Qt::QueuedConnection);
	};
	callbacks.onError = [this](const std::string &message) {
		if (!alive_.load())
			return;
		const QString error = QString::fromStdString(message);
		QMetaObject::invokeMethod(this, [this, error]() {
			setStatus(QStringLiteral("Error: %1").arg(error));
			emit errorOccurred(error);
		}, Qt::QueuedConnection);
	};
	callbacks.onState = [this](const std::string &state) {
		if (!alive_.load())
			return;
		const QString value = QString::fromStdString(state);
		QMetaObject::invokeMethod(this, [this, value]() {
			if (value == QStringLiteral("duplicate"))
				setStatus(QStringLiteral("Duplicate line skipped"));
			else if (value == QStringLiteral("no-text"))
				setStatus(QStringLiteral("No text in ROI"));
			else if (value == QStringLiteral("noise"))
				setStatus(QStringLiteral("Ignored non-dialogue text"));
		}, Qt::QueuedConnection);
	};

	std::string start_error;
	if (!pipeline_->start(config, ocr_, backend_, std::move(callbacks), &start_error))
	{
		if (error)
			*error = QString::fromStdString(start_error);
		return false;
	}
	return true;
}

void TranslatorManager::prepare()
{
	if (!alive_.load() || !enabled_.load())
		return;

	const int frame_width = frame_width_.load() > 0 ? frame_width_.load() : 1920;
	const int frame_height = frame_height_.load() > 0 ? frame_height_.load() : 1080;
	trans::Rect roi;
	{
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		roi = pixelRoi(frame_width, frame_height);
	}

	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	if (prepared_.load() && pipeline_ && pipeline_->running())
		return;

	ensureDictionary();
	if (!ensureBackend())
	{
		prepared_.store(false);
		emit preparedChanged();
		return;
	}

	QString error;
	if (!ensurePipeline(roi, &error))
	{
		prepared_.store(false);
		setStatus(error.isEmpty() ? QStringLiteral("Translator failed to start") : error);
		emit preparedChanged();
		return;
	}

	prepared_.store(true);
	emit preparedChanged();
	setStatus(QStringLiteral("Translator ready (%1)").arg(backend_id_));
}

void TranslatorManager::shutdown()
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	if (pipeline_)
		pipeline_->stop();
	if (prepared_.load())
	{
		prepared_.store(false);
		emit preparedChanged();
	}
}

void TranslatorManager::restartPipeline()
{
	prepared_.store(false);
	shutdown();
	prepare();
}

void TranslatorManager::manualTranslate()
{
	if (!enabled_.load())
	{
		setStatus(QStringLiteral("Enable the translator first"));
		return;
	}
	if (!prepared_.load())
		prepare();
	if (!prepared_.load())
		return;
	manual_requested_.store(true);
	last_tap_ms_.store(0);
	setStatus(QStringLiteral("Translating current frame..."));
}

void TranslatorManager::toggleOverlay()
{
	setOverlayVisible(!overlay_visible_);
}

void TranslatorManager::toggleBacklog()
{
	setBacklogVisible(!backlog_visible_);
}

void TranslatorManager::setRoiNormalized(qreal x, qreal y, qreal width, qreal height)
{
	x = std::clamp(clampUnit(x), 0.0, 0.95);
	y = std::clamp(clampUnit(y), 0.0, 0.95);
	width = std::clamp(width, 0.05, std::max(0.05, 1.0 - x));
	height = std::clamp(height, 0.05, std::max(0.05, 1.0 - y));
	{
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		roi_x_ = x;
		roi_y_ = y;
		roi_w_ = width;
		roi_h_ = height;
		saveRoi();
	}
	emit roiChanged();

	const int frame_width = frame_width_.load();
	const int frame_height = frame_height_.load();
	if (prepared_.load() && frame_width > 0 && frame_height > 0)
	{
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		std::lock_guard<std::mutex> lock(pipeline_mutex_);
		if (pipeline_)
			pipeline_->setRoi(pixelRoi(frame_width, frame_height));
	}
}

void TranslatorManager::savePreset(const QString &name)
{
	const QString trimmed = name.trimmed();
	if (trimmed.isEmpty())
		return;
	preset_name_ = trimmed;
	{
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		saveRoi();
	}
	emit presetNameChanged();
	setStatus(QStringLiteral("Saved ROI preset '%1'").arg(preset_name_));
}

void TranslatorManager::loadPreset(const QString &name)
{
	QSettings settings;
	const QString prefix = QStringLiteral("translator/preset/") + name;
	if (!settings.contains(prefix + QStringLiteral("/x")))
	{
		setStatus(QStringLiteral("No ROI preset named '%1'").arg(name));
		return;
	}
	preset_name_ = name;
	{
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		roi_x_ = settings.value(prefix + QStringLiteral("/x"), roi_x_).toDouble();
		roi_y_ = settings.value(prefix + QStringLiteral("/y"), roi_y_).toDouble();
		roi_w_ = settings.value(prefix + QStringLiteral("/w"), roi_w_).toDouble();
		roi_h_ = settings.value(prefix + QStringLiteral("/h"), roi_h_).toDouble();
	}
	settings.setValue(QStringLiteral("translator/preset"), preset_name_);
	emit presetNameChanged();
	emit roiChanged();

	const int frame_width = frame_width_.load();
	const int frame_height = frame_height_.load();
	if (prepared_.load() && frame_width > 0 && frame_height > 0)
	{
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		std::lock_guard<std::mutex> lock(pipeline_mutex_);
		if (pipeline_)
			pipeline_->setRoi(pixelRoi(frame_width, frame_height));
	}
	setStatus(QStringLiteral("Loaded ROI preset '%1'").arg(name));
}

QStringList TranslatorManager::presetNames() const
{
	QSettings settings;
	QStringList names = settings.value(QStringLiteral("translator/preset_names")).toStringList();
	if (!names.contains(QStringLiteral("default")))
		names.prepend(QStringLiteral("default"));
	names.removeDuplicates();
	return names;
}

void TranslatorManager::submitFrame(const AVFrame *frame)
{
	if (!alive_.load() || !enabled_.load() || !prepared_.load() || frame == nullptr)
		return;
	if (roi_selection_active_.load())
		return;

	const int64_t now = QDateTime::currentMSecsSinceEpoch();
	const bool manual = manual_requested_.exchange(false);
	const int64_t last = last_tap_ms_.load();
	if (!manual && last > 0 && now - last < kTapIntervalMs)
		return;
	last_tap_ms_.store(now);

	const AVFrame *source = frame;
	AVFrame *software = nullptr;
	if (frame->hw_frames_ctx)
	{
		software = av_frame_alloc();
		if (!software)
			return;
		if (av_hwframe_transfer_data(software, frame, 0) < 0)
		{
			av_frame_free(&software);
			return;
		}
		av_frame_copy_props(software, frame);
		source = software;
	}

	trans::FrameView view;
	trans::PixelFormat format;
	if (!ToTranslatorFormat(source->format, format) || source->width <= 0 || source->height <= 0)
	{
		av_frame_free(&software);
		if (now - last > 1000)
		{
			const QString format_name = QString::fromUtf8(
				av_get_pix_fmt_name(static_cast<AVPixelFormat>(source->format)));
			QMetaObject::invokeMethod(this, [this, format_name]() {
				setStatus(QStringLiteral("Unsupported video format for OCR: %1").arg(format_name));
			}, Qt::QueuedConnection);
		}
		return;
	}

	view.data[0] = source->data[0];
	view.data[1] = source->data[1];
	view.data[2] = source->data[2];
	view.stride[0] = source->linesize[0];
	view.stride[1] = source->linesize[1];
	view.stride[2] = source->linesize[2];
	view.width = source->width;
	view.height = source->height;
	view.format = format;

	if (frame_width_.load() != source->width || frame_height_.load() != source->height)
	{
		frame_width_.store(source->width);
		frame_height_.store(source->height);
		std::lock_guard<std::mutex> roi_lock(roi_mutex_);
		std::lock_guard<std::mutex> pipeline_lock(pipeline_mutex_);
		if (pipeline_)
			pipeline_->setRoi(pixelRoi(source->width, source->height));
	}

	{
		std::lock_guard<std::mutex> lock(pipeline_mutex_);
		if (!pipeline_ || !pipeline_->running())
		{
			av_frame_free(&software);
			return;
		}
		if (manual)
			pipeline_->manualTrigger();
		pipeline_->submitFrame(view, now);
	}

	av_frame_free(&software);
}

void TranslatorManager::onSessionEnded()
{
	std::lock_guard<std::mutex> lock(pipeline_mutex_);
	if (pipeline_)
		pipeline_->stop();
	prepared_.store(false);
	source_text_.clear();
	overlay_text_.clear();
	emit sourceTextChanged();
	emit overlayTextChanged();
	emit preparedChanged();
	setStatus(QStringLiteral("Session ended"));
}

void TranslatorManager::processTranslation(const trans::TranslationItem &item)
{
	overlay_text_ = QString::fromStdString(item.translated_text);
	source_text_ = QString::fromStdString(item.source_text);
	appendBacklog(item);
	emit overlayTextChanged();
	emit sourceTextChanged();
	setStatus(item.from_cache ? QStringLiteral("Translation from cache") : QStringLiteral("Translated"));
}

void TranslatorManager::appendBacklog(const trans::TranslationItem &item)
{
	QVariantMap entry;
	entry.insert(QStringLiteral("source"), QString::fromStdString(item.source_text));
	entry.insert(QStringLiteral("translation"), QString::fromStdString(item.translated_text));
	entry.insert(QStringLiteral("confidence"), item.ocr_confidence);
	entry.insert(QStringLiteral("timestamp"), static_cast<qlonglong>(item.translated_timestamp_ms));
	entry.insert(QStringLiteral("cached"), item.from_cache);
	backlog_.append(entry);
	while (backlog_.size() > kBacklogLimit)
		backlog_.removeFirst();
	emit backlogChanged();
}

void TranslatorManager::clearBacklog()
{
	backlog_.clear();
	emit backlogChanged();
}

void TranslatorManager::clearDictionaryLookup()
{
	dictionary_entries_.clear();
	dictionary_word_.clear();
	emit dictionaryEntriesChanged();
}

QVariantList TranslatorManager::entriesToVariant(const std::vector<trans::DictEntry> &entries) const
{
	QVariantList result;
	for (const trans::DictEntry &entry : entries)
	{
		QVariantMap map;
		map.insert(QStringLiteral("surface"), QString::fromStdString(entry.surface));
		map.insert(QStringLiteral("reading"), QString::fromStdString(entry.reading));
		map.insert(QStringLiteral("common"), entry.common);
		QVariantList glosses;
		for (const trans::DictGloss &gloss : entry.glosses)
		{
			QVariantMap gloss_map;
			gloss_map.insert(QStringLiteral("pos"), QString::fromStdString(gloss.part_of_speech));
			gloss_map.insert(QStringLiteral("text"), QString::fromStdString(gloss.text));
			gloss_map.insert(QStringLiteral("language"), QString::fromStdString(gloss.language));
			glosses.append(gloss_map);
		}
		map.insert(QStringLiteral("glosses"), glosses);
		result.append(map);
	}
	return result;
}

void TranslatorManager::lookupAt(const QString &text, int cursor_position)
{
	ensureDictionary();
	if (!dictionary_ || !dictionary_ready_)
		return;
	const int position = std::clamp(cursor_position, 0, static_cast<int>(text.size()));
	const QByteArray prefix = text.left(position).toUtf8();
	const QByteArray encoded = text.toUtf8();
	const std::vector<trans::DictEntry> entries = dictionary_->lookupAt(
		std::string(encoded.constData(), static_cast<size_t>(encoded.size())),
		static_cast<size_t>(prefix.size()));
	setDictionaryLookup(entries, text.mid(position, 1));
}

void TranslatorManager::lookupWord(const QString &word)
{
	ensureDictionary();
	if (!dictionary_ || !dictionary_ready_)
		return;
	const trans::DictResult exact = dictionary_->lookupExact(word.toStdString());
	std::vector<trans::DictEntry> entries = exact.entries;
	if (entries.empty())
		entries = dictionary_->lookupAt(word.toStdString(), 0);
	setDictionaryLookup(entries, word);
}

QString TranslatorManager::apiKey(const QString &backend_id) const
{
	QSettings settings;
	return settings.value(QStringLiteral("translator/api_key/") + backend_id).toString();
}

void TranslatorManager::setApiKey(const QString &backend_id, const QString &key)
{
	QSettings settings;
	settings.setValue(QStringLiteral("translator/api_key/") + backend_id, key.trimmed());
	settings.sync();
	if (backend_id == backend_id_ && prepared_.load())
		restartPipeline();
}

QString TranslatorManager::baseUrl(const QString &backend_id) const
{
	QSettings settings;
	return settings.value(QStringLiteral("translator/base_url/") + backend_id).toString();
}

void TranslatorManager::setBaseUrl(const QString &backend_id, const QString &url)
{
	QSettings settings;
	settings.setValue(QStringLiteral("translator/base_url/") + backend_id, url.trimmed());
	settings.sync();
	if (backend_id == backend_id_ && prepared_.load())
		restartPipeline();
}

QString TranslatorManager::model(const QString &backend_id) const
{
	// A flash-class chat-completions model from OpenCode's live /models list; subtitles
	// need low latency. Other providers have no default.
	const QString fallback = backend_id == QStringLiteral("opencode-go")
		? QStringLiteral("glm-5.3-flash") : QString();
	QSettings settings;
	return settings.value(QStringLiteral("translator/model/") + backend_id, fallback).toString();
}

void TranslatorManager::setModel(const QString &backend_id, const QString &model_name)
{
	QSettings settings;
	settings.setValue(QStringLiteral("translator/model/") + backend_id, model_name.trimmed());
	settings.sync();
	if (backend_id == backend_id_ && prepared_.load())
		restartPipeline();
}

void TranslatorManager::applySettings()
{
	if (prepared_.load())
		restartPipeline();
	else
		prepare();
}

void TranslatorManager::setNllbModelPath(const QString &path)
{
	if (nllb_model_path_ == path)
		return;
	nllb_model_path_ = path.trimmed();
	QSettings().setValue(QStringLiteral("translator/nllb_model"), nllb_model_path_);
	emit nllbModelPathChanged();
	if (backend_id_ == QStringLiteral("offline") && prepared_.load())
		restartPipeline();
}

void TranslatorManager::setPreprocess(const QString &mode)
{
	if (preprocess_ == mode)
		return;
	preprocess_ = mode;
	QSettings().setValue(QStringLiteral("translator/preprocess"), preprocess_);
	emit preprocessChanged();
	if (prepared_.load())
		restartPipeline();
}

void TranslatorManager::setDictionaryLookup(const std::vector<trans::DictEntry> &entries,
	const QString &fallback_word)
{
	dictionary_entries_ = entriesToVariant(entries);
	dictionary_word_ = entries.empty()
		? fallback_word
		: QString::fromStdString(entries.front().surface);
	emit dictionaryEntriesChanged();
	if (entries.empty())
		setStatus(QStringLiteral("No dictionary entry for '%1'").arg(fallback_word));
}
