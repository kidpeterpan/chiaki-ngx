// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Qt adapter between chiaki-ngx's QML frontend and the Qt-free translator core.
// Registered as the `Translator` QML singleton.
#pragma once

#include <QObject>
#include <trans/DictLookup.hpp>
#include <trans/Types.hpp>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

struct AVFrame;

namespace trans {
class TranslatorPipeline;
class OcrEngine;
class TranslatorBackend;
class JmDict;
struct TranslationItem;
} // namespace trans

class CloudTranslatorBackend;

class TranslatorManager : public QObject
{
	Q_OBJECT
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
	Q_PROPERTY(bool prepared READ prepared NOTIFY preparedChanged)
	Q_PROPERTY(bool overlayVisible READ overlayVisible WRITE setOverlayVisible NOTIFY overlayVisibleChanged)
	Q_PROPERTY(bool backlogVisible READ backlogVisible WRITE setBacklogVisible NOTIFY backlogVisibleChanged)
	Q_PROPERTY(bool roiSelectionActive READ roiSelectionActive WRITE setRoiSelectionActive NOTIFY roiSelectionActiveChanged)
	Q_PROPERTY(QString sourceText READ sourceText NOTIFY sourceTextChanged)
	Q_PROPERTY(QString overlayText READ overlayText NOTIFY overlayTextChanged)
	Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
	Q_PROPERTY(QString backendId READ backendId WRITE setBackendId NOTIFY backendIdChanged)
	Q_PROPERTY(QStringList backendIds READ backendIds CONSTANT)
	Q_PROPERTY(QString ocrProvider READ ocrProvider WRITE setOcrProvider NOTIFY ocrProviderChanged)
	Q_PROPERTY(QStringList ocrProviderIds READ ocrProviderIds CONSTANT)
	Q_PROPERTY(QString ocrApiKey READ ocrApiKey WRITE setOcrApiKey NOTIFY ocrApiKeyChanged)
	Q_PROPERTY(QString targetLanguage READ targetLanguage WRITE setTargetLanguage NOTIFY targetLanguageChanged)
	Q_PROPERTY(QVariantMap roiNormalized READ roiNormalized NOTIFY roiChanged)
	Q_PROPERTY(QVariantList backlog READ backlog NOTIFY backlogChanged)
	Q_PROPERTY(QVariantList dictionaryEntries READ dictionaryEntries NOTIFY dictionaryEntriesChanged)
	Q_PROPERTY(QString dictionaryWord READ dictionaryWord NOTIFY dictionaryEntriesChanged)
	Q_PROPERTY(bool dictionaryReady READ dictionaryReady NOTIFY dictionaryReadyChanged)
	Q_PROPERTY(QString presetName READ presetName NOTIFY presetNameChanged)
	Q_PROPERTY(QString preprocess READ preprocess NOTIFY preprocessChanged)
	Q_PROPERTY(QString nllbModelPath READ nllbModelPath NOTIFY nllbModelPathChanged)

public:
	explicit TranslatorManager(QObject *parent = nullptr);
	~TranslatorManager() override;

	bool enabled() const { return enabled_.load(); }
	bool prepared() const { return prepared_.load(); }
	bool overlayVisible() const { return overlay_visible_; }
	bool backlogVisible() const { return backlog_visible_; }
	bool roiSelectionActive() const { return roi_selection_active_.load(); }
	QString sourceText() const { return source_text_; }
	QString overlayText() const { return overlay_text_; }
	QString statusText() const { return status_text_; }
	QString backendId() const { return backend_id_; }
	QStringList backendIds() const { return QStringList{"gemini", "google", "google-cloud", "deepl", "opencode-go", "openai", "offline"}; }
	QString ocrProvider() const { return ocr_provider_; }
	QStringList ocrProviderIds() const { return QStringList{"paddle", "rapidocr", "google-vision", "ocrspace"}; }
	QString ocrApiKey() const { return ocr_api_key_; }
	QString targetLanguage() const { return target_language_; }
	QVariantMap roiNormalized() const;
	QVariantList backlog() const { return backlog_; }
	QVariantList dictionaryEntries() const { return dictionary_entries_; }
	QString dictionaryWord() const { return dictionary_word_; }
	bool dictionaryReady() const { return dictionary_ready_; }
	QString presetName() const { return preset_name_; }
	QString preprocess() const { return preprocess_; }
	QString nllbModelPath() const { return nllb_model_path_; }

	void setEnabled(bool enabled);
	void setOverlayVisible(bool visible);
	void setBacklogVisible(bool visible);
	void setRoiSelectionActive(bool active);
	void setBackendId(const QString &backend_id);
	void setOcrProvider(const QString &provider);
	void setOcrApiKey(const QString &key);
	void setTargetLanguage(const QString &language);

	// Called from the decoder thread with a frame that is still owned by the
	// caller. Only the ROI is copied, and only at the configured sample rate.
	void submitFrame(const AVFrame *frame);
	void onSessionEnded();

public slots:
	void prepare();
	void shutdown();
	void manualTranslate();
	void toggleOverlay();
	void toggleBacklog();
	void setRoiNormalized(qreal x, qreal y, qreal width, qreal height);
	void savePreset(const QString &name);
	void loadPreset(const QString &name);
	QStringList presetNames() const;
	void lookupAt(const QString &text, int cursor_position);
	void lookupWord(const QString &word);
	void clearDictionaryLookup();
	void clearBacklog();
	void setApiKey(const QString &backend_id, const QString &key);
	QString apiKey(const QString &backend_id) const;
	// Settings for the OpenAI-compatible providers ("opencode-go", "openai").
	QString baseUrl(const QString &backend_id) const;
	void setBaseUrl(const QString &backend_id, const QString &url);
	QString model(const QString &backend_id) const;
	void setModel(const QString &backend_id, const QString &model_name);
	void applySettings();
	void setPreprocess(const QString &mode);
	void setNllbModelPath(const QString &path);

signals:
	void enabledChanged();
	void preparedChanged();
	void overlayVisibleChanged();
	void backlogVisibleChanged();
	void roiSelectionActiveChanged();
	void sourceTextChanged();
	void overlayTextChanged();
	void statusTextChanged();
	void backendIdChanged();
	void ocrProviderChanged();
	void ocrApiKeyChanged();
	void targetLanguageChanged();
	void roiChanged();
	void backlogChanged();
	void dictionaryEntriesChanged();
	void dictionaryReadyChanged();
	void presetNameChanged();
	void preprocessChanged();
	void nllbModelPathChanged();
	void errorOccurred(const QString &message);

private:
	void ensureDictionary();
	bool ensureBackend();
	bool ensurePipeline(const trans::Rect &roi, QString *error);
	void restartPipeline();
	void processTranslation(const trans::TranslationItem &item);
	void appendBacklog(const trans::TranslationItem &item);
	void setStatus(const QString &status);
	QVariantList entriesToVariant(const std::vector<trans::DictEntry> &entries) const;
	void setDictionaryLookup(const std::vector<trans::DictEntry> &entries, const QString &fallback_word);
	QString assetPath(const QString &relative) const;
	QString settingsKey(const QString &suffix) const;
	qreal clampUnit(qreal value) const;
	trans::Rect pixelRoi(int width, int height) const;
	void saveRoi();

	std::mutex pipeline_mutex_;
	std::unique_ptr<trans::TranslatorPipeline> pipeline_;
	std::shared_ptr<trans::OcrEngine> ocr_;
	std::shared_ptr<trans::TranslatorBackend> backend_;
	std::shared_ptr<CloudTranslatorBackend> cloud_backend_;
	std::unique_ptr<trans::JmDict> dictionary_;

	std::atomic<bool> enabled_{false};
	std::atomic<bool> manual_requested_{false};
	std::atomic<int64_t> last_tap_ms_{0};
	std::atomic<int> frame_width_{0};
	std::atomic<int> frame_height_{0};
	std::atomic<bool> alive_{true};

	std::atomic<bool> prepared_{false};
	bool overlay_visible_ = true;
	bool backlog_visible_ = false;
	std::atomic<bool> roi_selection_active_{false};
	bool dictionary_ready_ = false;
	QString source_text_;
	QString overlay_text_;
	QString status_text_ = QStringLiteral("Translator idle");
	QString backend_id_;
	QString ocr_provider_;
	QString ocr_api_key_;
	QString target_language_;
	QString preprocess_;
	QString nllb_model_path_;
	QString preset_name_ = QStringLiteral("default");
	mutable std::mutex roi_mutex_;
	qreal roi_x_ = 0.05;
	qreal roi_y_ = 0.62;
	qreal roi_w_ = 0.90;
	qreal roi_h_ = 0.30;
	QVariantList backlog_;
	QVariantList dictionary_entries_;
	QString dictionary_word_;
};
