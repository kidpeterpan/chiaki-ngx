// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/cloudbackends.h"

#include <trans/TextProcessor.hpp>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {

QString LanguageName(const QString &code)
{
	const QString normalized = code.trimmed().toLower();
	if (normalized == QStringLiteral("ja") || normalized == QStringLiteral("jpn"))
		return QStringLiteral("Japanese");
	if (normalized == QStringLiteral("th") || normalized == QStringLiteral("tha"))
		return QStringLiteral("Thai");
	if (normalized == QStringLiteral("en") || normalized == QStringLiteral("eng"))
		return QStringLiteral("English");
	if (normalized == QStringLiteral("ko") || normalized == QStringLiteral("kor"))
		return QStringLiteral("Korean");
	if (normalized == QStringLiteral("zh") || normalized == QStringLiteral("zho"))
		return QStringLiteral("Chinese");
	return code;
}

QString CleanTranslation(QString text)
{
	text = text.trimmed();
	while (text.size() >= 2 && text.startsWith('"') && text.endsWith('"'))
		text = text.mid(1, text.size() - 2).trimmed();
	if (text.startsWith(QStringLiteral("```")) && text.endsWith(QStringLiteral("```")))
	{
		const int first_newline = text.indexOf('\n');
		if (first_newline > 0)
			text = text.mid(first_newline + 1, text.size() - first_newline - 4).trimmed();
	}
	return text;
}

QString Shorten(const QByteArray &body)
{
	QString text = QString::fromUtf8(body).simplified();
	if (text.size() > 240)
		text = text.left(240) + QStringLiteral("...");
	return text;
}

bool IsOpenAiProvider(const QString &provider)
{
	return provider == QStringLiteral("opencode-go") || provider == QStringLiteral("openai");
}

bool IsLocalHost(const QUrl &url)
{
	const QString host = url.host().toLower();
	return host == QStringLiteral("localhost") || host == QStringLiteral("127.0.0.1") ||
		host == QStringLiteral("::1");
}

struct PromptTexts
{
	QString system;
	QString user;
};

// Shared by every LLM provider so they all get the same 3-line dialogue context.
PromptTexts BuildPromptTexts(const trans::TranslationRequest &request)
{
	trans::PromptConfig prompt_config;
	prompt_config.source_language = LanguageName(QString::fromStdString(request.source_language)).toStdString();
	prompt_config.target_language = LanguageName(QString::fromStdString(request.target_language)).toStdString();

	trans::ConversationContext context(3);
	for (const trans::ContextPair &pair : request.context)
		context.add(pair.source, pair.translation);

	return {QString::fromStdString(trans::buildSystemPrompt(prompt_config)),
		QString::fromStdString(trans::buildUserPrompt(request.text, context, prompt_config))};
}

} // namespace

CloudTranslatorBackend::CloudTranslatorBackend(CloudBackendOptions options, QObject *parent)
	: QObject(parent), options_(std::move(options)), network_(new QNetworkAccessManager(this))
{
	name_ = options_.provider.toUtf8();
	if (options_.model.isEmpty() && !IsOpenAiProvider(options_.provider))
		options_.model = QStringLiteral("gemini-2.5-flash");
	if (!options_.gemini_base_url.isValid() || options_.gemini_base_url.isEmpty())
		options_.gemini_base_url = QUrl(QStringLiteral("https://generativelanguage.googleapis.com/v1beta/models/"));
	if (!options_.deepl_endpoint.isValid() || options_.deepl_endpoint.isEmpty())
		options_.deepl_endpoint = QUrl(QStringLiteral("https://api-free.deepl.com/v2/translate"));
	if (options_.timeout_ms <= 0)
		options_.timeout_ms = 20000;
}

CloudTranslatorBackend::~CloudTranslatorBackend() = default;

QUrl CloudTranslatorBackend::effectiveOpenAiBaseUrl(const CloudBackendOptions &options)
{
	if (options.openai_base_url.isValid() && !options.openai_base_url.isEmpty())
		return options.openai_base_url;
	if (options.provider == QStringLiteral("opencode-go"))
		return QUrl(QStringLiteral("https://opencode.ai/inference/go/openai/v1"));
	return QUrl();
}

QString CloudTranslatorBackend::validateOptions(const CloudBackendOptions &options)
{
	if (!IsOpenAiProvider(options.provider))
		return QString();
	const QUrl base = effectiveOpenAiBaseUrl(options);
	if (base.isEmpty())
		return QStringLiteral("Set a base URL for the OpenAI-compatible backend");
	const QString scheme = base.scheme().toLower();
	const bool local = IsLocalHost(base);
	if (!(scheme == QStringLiteral("https") || (scheme == QStringLiteral("http") && local)))
		return QStringLiteral("Base URL must use https (http is allowed only for localhost)");
	if (options.model.trimmed().isEmpty())
		return QStringLiteral("Set a model name");
	const bool key_required = options.provider == QStringLiteral("opencode-go") || !local;
	if (key_required && options.api_key.isEmpty())
		return QStringLiteral("Add a %1 API key in translator settings").arg(options.provider);
	return QString();
}

void CloudTranslatorBackend::translate(const trans::TranslationRequest &request,
	trans::TranslationCallback callback)
{
	// The pipeline calls this from its worker thread; QNetworkAccessManager must
	// stay on the thread that created it.
	QMetaObject::invokeMethod(this, [this, request, callback]() mutable {
		startRequest(request, std::move(callback));
	}, Qt::QueuedConnection);
}

void CloudTranslatorBackend::startRequest(const trans::TranslationRequest &request,
	trans::TranslationCallback callback)
{
	if (options_.provider == QStringLiteral("deepl"))
		startDeepl(request, std::move(callback));
	else if (IsOpenAiProvider(options_.provider))
		startOpenAiChat(request, std::move(callback));
	else
		startGemini(request, std::move(callback));
}

void CloudTranslatorBackend::startGemini(const trans::TranslationRequest &request,
	trans::TranslationCallback callback)
{
	const PromptTexts prompts = BuildPromptTexts(request);
	const QJsonObject system_object{{"parts", QJsonArray{QJsonObject{{"text", prompts.system}}}}};
	const QJsonObject user_object{{"role", QStringLiteral("user")}, {"parts", QJsonArray{QJsonObject{
		{"text", prompts.user}}}}};
	const QJsonObject generation{{"temperature", 0.2}, {"maxOutputTokens", 400}};
	const QJsonObject body{{"systemInstruction", system_object},
		{"contents", QJsonArray{user_object}}, {"generationConfig", generation}};

	QString base = options_.gemini_base_url.toString();
	if (!base.endsWith('/'))
		base += '/';
	QUrl url(base + options_.model + QStringLiteral(":generateContent"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("key"), options_.api_key);
	url.setQuery(query);

	QNetworkRequest network_request(url);
	network_request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	QNetworkReply *reply = network_->post(network_request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	auto *timer = new QTimer(reply);
	timer->setSingleShot(true);
	QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
	timer->start(options_.timeout_ms);
	const qint64 started_ms = QDateTime::currentMSecsSinceEpoch();
	QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback, started_ms]() mutable {
		finish(reply, std::move(callback), started_ms);
	});
}

void CloudTranslatorBackend::startOpenAiChat(const trans::TranslationRequest &request,
	trans::TranslationCallback callback)
{
	const QString problem = validateOptions(options_);
	if (!problem.isEmpty())
	{
		trans::TranslationResult result;
		result.ok = false;
		result.error = problem.toStdString();
		if (callback)
			callback(std::move(result));
		return;
	}

	const PromptTexts prompts = BuildPromptTexts(request);
	const QJsonArray messages{
		QJsonObject{{"role", QStringLiteral("system")}, {"content", prompts.system}},
		QJsonObject{{"role", QStringLiteral("user")}, {"content", prompts.user}}};
	const QJsonObject body{{"model", options_.model}, {"messages", messages},
		{"temperature", 0.2}, {"max_tokens", 400}};

	QUrl url = effectiveOpenAiBaseUrl(options_);
	QString path = url.path();
	while (path.endsWith('/'))
		path.chop(1);
	url.setPath(path + QStringLiteral("/chat/completions"));

	QNetworkRequest network_request(url);
	network_request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	if (!options_.api_key.isEmpty())
		network_request.setRawHeader("Authorization", "Bearer " + options_.api_key.toUtf8());
	QNetworkReply *reply = network_->post(network_request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	auto *timer = new QTimer(reply);
	timer->setSingleShot(true);
	QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
	timer->start(options_.timeout_ms);
	const qint64 started_ms = QDateTime::currentMSecsSinceEpoch();
	QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback, started_ms]() mutable {
		finish(reply, std::move(callback), started_ms);
	});
}

void CloudTranslatorBackend::startDeepl(const trans::TranslationRequest &request,
	trans::TranslationCallback callback)
{
	QNetworkRequest network_request(options_.deepl_endpoint);
	network_request.setHeader(QNetworkRequest::ContentTypeHeader,
		QStringLiteral("application/x-www-form-urlencoded"));
	network_request.setRawHeader("Authorization", "DeepL-Auth-Key " + options_.api_key.toUtf8());

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("text"), QString::fromStdString(request.text));
	form.addQueryItem(QStringLiteral("source_lang"),
		QString::fromStdString(request.source_language).toUpper());
	form.addQueryItem(QStringLiteral("target_lang"),
		QString::fromStdString(request.target_language).toUpper());
	QNetworkReply *reply = network_->post(network_request, form.toString(QUrl::FullyEncoded).toUtf8());
	auto *timer = new QTimer(reply);
	timer->setSingleShot(true);
	QObject::connect(timer, &QTimer::timeout, reply, &QNetworkReply::abort);
	timer->start(options_.timeout_ms);
	const qint64 started_ms = QDateTime::currentMSecsSinceEpoch();
	QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, callback, started_ms]() mutable {
		finish(reply, std::move(callback), started_ms);
	});
}

void CloudTranslatorBackend::finish(QNetworkReply *reply, trans::TranslationCallback callback,
	qint64 started_ms)
{
	trans::TranslationResult result;
	result.latency_ms = QDateTime::currentMSecsSinceEpoch() - started_ms;
	const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
	// Redact the key before anything is shortened for display: a key cut in half by the
	// truncation would no longer match and its prefix would survive.
	auto redact = [this](QString text) {
		if (!options_.api_key.isEmpty())
			text.replace(options_.api_key, QStringLiteral("***"));
		return text;
	};
	const QString body_excerpt = Shorten(redact(QString::fromUtf8(body)).toUtf8());
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

	if (reply->error() != QNetworkReply::NoError)
	{
		result.ok = false;
		QString message = reply->error() == QNetworkReply::OperationCanceledError
			? QStringLiteral("translation request timed out")
			: redact(reply->errorString());
		if (status >= 400)
			message += QStringLiteral(" (HTTP %1: %2)").arg(status).arg(body_excerpt);
		result.error = message.toStdString();
	}
	else if (status >= 400)
	{
		result.ok = false;
		result.error = QStringLiteral("HTTP %1: %2").arg(status).arg(body_excerpt).toStdString();
	}
	else
	{
		const QJsonDocument document = QJsonDocument::fromJson(body);
		const QJsonObject root = document.object();
		if (options_.provider == QStringLiteral("deepl"))
		{
			const QJsonArray translations = root.value(QStringLiteral("translations")).toArray();
			if (!translations.isEmpty())
				result.text = CleanTranslation(
					translations.first().toObject().value(QStringLiteral("text")).toString()).toStdString();
		}
		else if (IsOpenAiProvider(options_.provider))
		{
			const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
			if (!choices.isEmpty())
			{
				const QJsonObject message = choices.first().toObject()
					.value(QStringLiteral("message")).toObject();
				const QJsonValue content = message.value(QStringLiteral("content"));
				if (content.isString())
					result.text = CleanTranslation(content.toString()).toStdString();
				const QJsonValue reasoning = message.value(QStringLiteral("reasoning_content"));
				if (result.text.empty() && reasoning.isString() && !reasoning.toString().trimmed().isEmpty())
					result.error = "model returned reasoning but no content; pick a non-reasoning model";
			}
		}
		else
		{
			const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
			if (!candidates.isEmpty())
			{
				const QJsonArray parts = candidates.first().toObject()
					.value(QStringLiteral("content")).toObject()
					.value(QStringLiteral("parts")).toArray();
				if (!parts.isEmpty())
					result.text = CleanTranslation(parts.first().toObject()
						.value(QStringLiteral("text")).toString()).toStdString();
			}
			if (result.text.empty())
			{
				const QJsonObject feedback = root.value(QStringLiteral("promptFeedback")).toObject();
				if (!feedback.isEmpty())
					result.error = (QStringLiteral("Gemini blocked the request: ") +
						feedback.value(QStringLiteral("blockReason")).toString()).toStdString();
			}
		}

		if (!result.text.empty())
		{
			result.ok = true;
		}
		else if (result.error.empty())
		{
			result.error = QStringLiteral("translation response was empty").toStdString();
		}
	}

	reply->deleteLater();
	if (!options_.api_key.isEmpty() && !result.error.empty())
		result.error = QString::fromStdString(result.error).replace(options_.api_key, QStringLiteral("***")).toStdString();
	if (callback)
		callback(std::move(result));
}
