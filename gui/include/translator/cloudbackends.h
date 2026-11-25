// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Cloud translation backends (Gemini Flash, DeepL and OpenAI-compatible chat
// endpoints such as OpenCode Go) for the Qt frontend.
// translate() may be called from the pipeline worker; the actual network work
// is marshalled onto the backend's Qt thread.
#pragma once

#include <trans/TranslatorBackend.hpp>

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

struct CloudBackendOptions
{
	QString provider = QStringLiteral("gemini");
	QString api_key;
	QString model;
	QUrl gemini_base_url = QUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/"));
	QUrl deepl_endpoint = QUrl(QStringLiteral("https://api-free.deepl.com/v2/translate"));
	// Providers "opencode-go" and "openai". Empty means the provider default.
	QUrl openai_base_url;
	// Google Translate's unofficial web endpoint (no key) and the Cloud
	// Translation v2 endpoint (API key). Overridable for tests.
	QUrl google_translate_endpoint = QUrl(QStringLiteral("https://translate.googleapis.com/translate_a/single"));
	QUrl google_cloud_endpoint = QUrl(QStringLiteral("https://translation.googleapis.com/language/translate/v2"));
	int timeout_ms = 20000;
};

class CloudTranslatorBackend : public QObject, public trans::TranslatorBackend
{
	Q_OBJECT

public:
	explicit CloudTranslatorBackend(CloudBackendOptions options, QObject *parent = nullptr);
	~CloudTranslatorBackend() override;

	void translate(const trans::TranslationRequest &request, trans::TranslationCallback callback) override;
	const char *name() const override { return name_.constData(); }
	bool offline() const override { return false; }

	// Effective base URL for the OpenAI-compatible providers: the override when set,
	// else the opencode-go default, else an empty URL.
	static QUrl effectiveOpenAiBaseUrl(const CloudBackendOptions &options);
	// Empty when the options are usable, otherwise a user-facing message. Only inspects
	// the providers "opencode-go" and "openai"; any other provider returns empty.
	static QString validateOptions(const CloudBackendOptions &options);

private:
	void startRequest(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startGemini(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startDeepl(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startOpenAiChat(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startGoogleFree(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startGoogleCloud(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void finish(QNetworkReply *reply, trans::TranslationCallback callback, qint64 started_ms);

	CloudBackendOptions options_;
	QByteArray name_;
	QNetworkAccessManager *network_ = nullptr;
};
