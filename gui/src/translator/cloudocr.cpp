// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/cloudocr.h"

#include <QBuffer>
#include <QEventLoop>
#include <QHttpMultiPart>
#include <QImage>
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

void AddTextLines(const QString &text, const trans::Image &image, std::vector<trans::OcrLine> &lines)
{
	const QStringList parts = text.split('\n');
	QStringList visible_parts;
	for (const QString &part : parts)
	{
		if (!part.trimmed().isEmpty())
			visible_parts.push_back(part.trimmed());
	}
	const int count = std::max(1, static_cast<int>(visible_parts.size()));
	int index = 0;
	for (const QString &part : visible_parts)
	{
		trans::OcrLine line;
		line.text = part.toStdString();
		line.confidence = 0.9f;
		line.box = trans::Rect{0, index * image.height / count, image.width,
			std::max(1, image.height / count)};
		lines.push_back(std::move(line));
		++index;
	}
}

QByteArray EncodePng(const trans::Image &image)
{
	if (image.empty())
		return {};
	const QImage view(image.pixels.data(), image.width, image.height, image.stride,
		QImage::Format_RGB888);
	QByteArray encoded;
	QBuffer buffer(&encoded);
	buffer.open(QIODevice::WriteOnly);
	view.save(&buffer, "PNG");
	return encoded;
}

} // namespace

bool CloudOcrEngine::load(const trans::OcrConfig &config, std::string *error)
{
	provider_ = config.cloud_provider;
	api_key_ = config.cloud_api_key;
	endpoint_ = config.cloud_endpoint;
	language_ = config.cloud_language.empty() ? "jpn" : config.cloud_language;
	target_language_ = config.cloud_target_language.empty() ? "th" : config.cloud_target_language;
	timeout_ms_ = config.cloud_timeout_ms > 0 ? config.cloud_timeout_ms : 20000;
	ready_ = false;

	if (provider_ == "google-vision")
	{
		name_ = "google-vision";
		if (endpoint_.empty())
			endpoint_ = "https://vision.googleapis.com/v1/images:annotate";
		if (api_key_.empty())
		{
			if (error)
				*error = "Google Cloud Vision requires an API key";
			return false;
		}
	}
	else if (provider_ == "gemini-vision")
	{
		name_ = "gemini-vision";
		if (endpoint_.empty())
			endpoint_ = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent";
		if (api_key_.empty())
		{
			if (error)
				*error = "Gemini Vision requires an API key";
			return false;
		}
	}
	else if (provider_ == "ocrspace")
	{
		name_ = "ocrspace";
		if (endpoint_.empty())
			endpoint_ = "https://api.ocr.space/parse/image";
		if (api_key_.empty())
			api_key_ = "helloworld";
	}
	else
	{
		if (error)
			*error = "unsupported cloud OCR provider: " + provider_;
		return false;
	}
	ready_ = true;
	return true;
}

std::vector<trans::OcrLine> CloudOcrEngine::recognize(const trans::Image &image, std::string *error)
{
	return recognizeOnThread(image, error);
}

std::vector<trans::OcrLine> CloudOcrEngine::recognizeOnThread(const trans::Image &image, std::string *error)
{
	std::vector<trans::OcrLine> lines;
	if (!ready_ || image.empty())
		return lines;
	const QByteArray png = EncodePng(image);
	if (png.isEmpty())
	{
		if (error)
			*error = "failed to encode the OCR crop as PNG";
		return lines;
	}

	// This runs on the pipeline worker. A local manager and event loop keep the
	// network object on the calling thread; the GUI thread is never blocked.
	QNetworkAccessManager network;
	QNetworkReply *reply = nullptr;
	QHttpMultiPart *multipart = nullptr;

	if (provider_ == "gemini-vision")
	{
		const QString prompt = QStringLiteral(
			"Detect every text region in this game screenshot. Return ONLY a JSON array. "
			"Each element must have: \"text\" (the original Japanese text), "
			"\"translatedText\" (the translation to %1), and "
			"\"box_2d\": [ymin, xmin, ymax, xmax] normalized to 0-1000. "
			"Do not use markdown fences.").arg(QString::fromStdString(target_language_));
		QUrl url(QString::fromStdString(endpoint_));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("key"), QString::fromStdString(api_key_));
		url.setQuery(query);
		const QJsonObject part_text{{QStringLiteral("text"), prompt}};
		const QJsonObject part_image{
			{QStringLiteral("inline_data"), QJsonObject{
				{QStringLiteral("mime_type"), QStringLiteral("image/png")},
				{QStringLiteral("data"), QString::fromLatin1(png.toBase64())}}}};
		const QJsonObject body{
			{QStringLiteral("contents"), QJsonArray{QJsonObject{
				{QStringLiteral("parts"), QJsonArray{part_text, part_image}}}}},
			{QStringLiteral("generationConfig"), QJsonObject{
				{QStringLiteral("temperature"), 0.1},
				{QStringLiteral("maxOutputTokens"), 2048}}},
		};
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		reply = network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	}
	else if (provider_ == "google-vision")
	{
		QUrl url(QString::fromStdString(endpoint_));
		QUrlQuery query;
		query.addQueryItem(QStringLiteral("key"), QString::fromStdString(api_key_));
		url.setQuery(query);
		const QJsonObject request_body{
			{QStringLiteral("requests"), QJsonArray{QJsonObject{
				{QStringLiteral("image"), QJsonObject{{QStringLiteral("content"),
					QString::fromLatin1(png.toBase64())}}},
				{QStringLiteral("features"), QJsonArray{QJsonObject{
					{QStringLiteral("type"), QStringLiteral("DOCUMENT_TEXT_DETECTION")},
					{QStringLiteral("maxResults"), 50}}}},
			}}},
		};
		QNetworkRequest request(url);
		request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
		reply = network.post(request, QJsonDocument(request_body).toJson(QJsonDocument::Compact));
	}
	else
	{
		multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
		auto add_part = [multipart](const QByteArray &name, const QByteArray &value) {
			QHttpPart part;
			part.setHeader(QNetworkRequest::ContentDispositionHeader,
				QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
			part.setBody(value);
			multipart->append(part);
		};
		add_part("apikey", api_key_.c_str());
		add_part("language", language_.c_str());
		add_part("isOverlayRequired", "false");
		QHttpPart file_part;
		file_part.setHeader(QNetworkRequest::ContentDispositionHeader,
			QStringLiteral("form-data; name=\"file\"; filename=\"roi.png\""));
		file_part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
		file_part.setBody(png);
		multipart->append(file_part);
		QNetworkRequest request(QUrl(QString::fromStdString(endpoint_)));
		reply = network.post(request, multipart);
		multipart->setParent(reply);
	}

	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
	timer.start(timeout_ms_);
	loop.exec();

	const QByteArray body = reply->isOpen() ? reply->readAll() : QByteArray();
	const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const QString reply_error = reply->error() == QNetworkReply::NoError
		? QString()
		: (reply->error() == QNetworkReply::OperationCanceledError
			? QStringLiteral("OCR request timed out")
			: reply->errorString());
	delete reply;

	if (!reply_error.isEmpty())
	{
		if (error)
			*error = reply_error.toStdString();
		return lines;
	}
	if (status >= 400)
	{
		if (error)
			*error = QStringLiteral("OCR HTTP %1: %2").arg(status)
				.arg(QString::fromUtf8(body.left(240))).toStdString();
		return lines;
	}

	const QJsonDocument document = QJsonDocument::fromJson(body);
	const QJsonObject root = document.object();
	QString text;
	if (provider_ == "gemini-vision")
	{
		const QJsonArray candidates = root.value(QStringLiteral("candidates")).toArray();
		if (!candidates.isEmpty())
		{
			const QJsonArray parts = candidates.first().toObject()
				.value(QStringLiteral("content")).toObject()
				.value(QStringLiteral("parts")).toArray();
			if (!parts.isEmpty())
				text = parts.first().toObject().value(QStringLiteral("text")).toString();
		}
		const int start = text.indexOf('[');
		const int end = text.lastIndexOf(']');
		if (start < 0 || end <= start)
		{
			if (error)
				*error = "Gemini Vision returned no JSON regions";
			return lines;
		}
		QJsonParseError parse_error;
		const QJsonDocument regions_document = QJsonDocument::fromJson(
			text.mid(start, end - start + 1).toUtf8(), &parse_error);
		if (parse_error.error != QJsonParseError::NoError || !regions_document.isArray())
		{
			if (error)
				*error = "Gemini Vision returned malformed JSON";
			return lines;
		}
		for (const QJsonValue &value : regions_document.array())
		{
			const QJsonObject region = value.toObject();
			const QString source = region.value(QStringLiteral("text")).toString().trimmed();
			const QString translated = region.value(QStringLiteral("translatedText")).toString().trimmed();
			if (source.isEmpty() || translated.isEmpty())
				continue;
			trans::OcrLine line;
			line.text = source.toStdString();
			line.translated_text = translated.toStdString();
			line.confidence = 0.9f;
			line.box = trans::Rect{0, 0, image.width, image.height};
			const QJsonArray box = region.value(QStringLiteral("box_2d")).toArray();
			if (box.size() == 4)
			{
				const qreal ymin = box.at(0).toDouble() / 1000.0;
				const qreal xmin = box.at(1).toDouble() / 1000.0;
				const qreal ymax = box.at(2).toDouble() / 1000.0;
				const qreal xmax = box.at(3).toDouble() / 1000.0;
				line.box = trans::Rect{
					static_cast<int>(xmin * image.width),
					static_cast<int>(ymin * image.height),
					std::max(1, static_cast<int>((xmax - xmin) * image.width)),
					std::max(1, static_cast<int>((ymax - ymin) * image.height))};
			}
			lines.push_back(std::move(line));
		}
		if (lines.empty() && error)
			*error = "Gemini Vision returned no translated regions";
		return lines;
	}
	if (provider_ == "google-vision")
	{
		const QJsonArray responses = root.value(QStringLiteral("responses")).toArray();
		if (!responses.isEmpty())
		{
			const QJsonObject response = responses.first().toObject();
			text = response.value(QStringLiteral("fullTextAnnotation")).toObject()
				.value(QStringLiteral("text")).toString();
			if (text.isEmpty())
			{
				const QJsonArray annotations = response.value(QStringLiteral("textAnnotations")).toArray();
				if (!annotations.isEmpty())
					text = annotations.first().toObject().value(QStringLiteral("description")).toString();
			}
		}
	}
	else
	{
		const QJsonArray results = root.value(QStringLiteral("ParsedResults")).toArray();
		if (!results.isEmpty())
			text = results.first().toObject().value(QStringLiteral("ParsedText")).toString();
	}

	AddTextLines(text, image, lines);
	if (lines.empty() && error)
		*error = "cloud OCR returned no text";
	return lines;
}
