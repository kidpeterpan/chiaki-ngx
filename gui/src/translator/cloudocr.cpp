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
	for (const QString &part : parts)
	{
		const QString trimmed = part.trimmed();
		if (trimmed.isEmpty())
			continue;
		trans::OcrLine line;
		line.text = trimmed.toStdString();
		line.confidence = 0.9f;
		line.box = trans::Rect{0, 0, image.width, image.height};
		lines.push_back(std::move(line));
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

	if (provider_ == "google-vision")
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
