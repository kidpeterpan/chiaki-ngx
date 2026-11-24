// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Cloud translation backends (Gemini Flash and DeepL) for the Qt frontend.
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

private:
	void startRequest(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startGemini(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void startDeepl(const trans::TranslationRequest &request, trans::TranslationCallback callback);
	void finish(QNetworkReply *reply, trans::TranslationCallback callback, qint64 started_ms);

	CloudBackendOptions options_;
	QByteArray name_;
	QNetworkAccessManager *network_ = nullptr;
};
