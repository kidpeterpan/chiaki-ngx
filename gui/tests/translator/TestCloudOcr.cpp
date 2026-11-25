// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Local mock tests for the Google Cloud Vision / OCR.space adapters.
#include "translator/cloudocr.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>

#include <cstdio>
#include <memory>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

struct MockServer
{
	QTcpServer server;
	QByteArray response_body = "{}";
	int status = 200;

	bool start()
	{
		QObject::connect(&server, &QTcpServer::newConnection, [this]() {
			while (QTcpSocket *socket = server.nextPendingConnection())
			{
				auto buffer = std::make_shared<QByteArray>();
				QObject::connect(socket, &QTcpSocket::readyRead, [this, socket, buffer]() {
					buffer->append(socket->readAll());
					const int header_end = buffer->indexOf("\r\n\r\n");
					if (header_end < 0)
						return;
					int content_length = 0;
					for (const QByteArray &line : buffer->left(header_end).split('\n'))
					{
						const QByteArray trimmed = line.trimmed();
						if (trimmed.toLower().startsWith("content-length:"))
							content_length = trimmed.mid(15).trimmed().toInt();
					}
					if (buffer->size() < header_end + 4 + content_length)
						return;
					const QByteArray header = "HTTP/1.1 " + QByteArray::number(status) +
						" Mock\r\nContent-Type: application/json\r\nContent-Length: " +
						QByteArray::number(response_body.size()) + "\r\nConnection: close\r\n\r\n";
					socket->write(header + response_body);
					socket->flush();
					socket->disconnectFromHost();
				});
			}
		});
		return server.listen(QHostAddress::LocalHost, 0);
	}

	QString url(const QString &path) const
	{
		return QStringLiteral("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path);
	}
};

trans::Image TestImage()
{
	trans::Image image;
	image.width = 8;
	image.height = 8;
	image.stride = 8 * 3;
	image.pixels.assign(8 * 8 * 3, 200);
	return image;
}

int Fail(const char *message)
{
	std::fprintf(stderr, "FAIL: %s\n", message);
	return 1;
}

} // namespace

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server did not listen");
		mock.response_body = R"({"responses":[{"fullTextAnnotation":{"text":"ゲームを続けよう\n二行目"}}]})";
		CloudOcrEngine engine;
		std::string error;
		trans::OcrConfig config;
		config.cloud_provider = "google-vision";
		config.cloud_api_key = "test-key";
		config.cloud_endpoint = mock.url(QStringLiteral("/vision")).toStdString();
		if (!engine.load(config, &error))
			return Fail("google vision load failed");
		const auto lines = engine.recognize(TestImage(), &error);
		if (lines.size() != 2 || lines[0].text != "ゲームを続けよう" || lines[1].text != "二行目")
		{
			std::fprintf(stderr, "google lines=%zu error=%s\n", lines.size(), error.c_str());
			return Fail("google vision text was not parsed");
		}
		std::printf("PASS google vision cloud OCR\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server did not listen");
		const QString regions = QStringLiteral(
			R"([{"text":"ゲームを続けよう","translatedText":"เล่นเกมต่อ","box_2d":[100,200,150,700]}])");
		const QJsonObject root{
			{QStringLiteral("candidates"), QJsonArray{QJsonObject{
				{QStringLiteral("content"), QJsonObject{
					{QStringLiteral("parts"), QJsonArray{QJsonObject{
						{QStringLiteral("text"), regions}}}}}}}}},
		};
		mock.response_body = QJsonDocument(root).toJson(QJsonDocument::Compact);
		CloudOcrEngine engine;
		std::string error;
		trans::OcrConfig config;
		config.cloud_provider = "gemini-vision";
		config.cloud_api_key = "test-key";
		config.cloud_target_language = "th";
		config.cloud_endpoint = mock.url(QStringLiteral("/gemini")).toStdString();
		if (!engine.load(config, &error))
			return Fail("gemini vision load failed");
		const auto lines = engine.recognize(TestImage(), &error);
		if (lines.size() != 1 || lines[0].text != "ゲームを続けよう" ||
				lines[0].translated_text != "เล่นเกมต่อ")
		{
			std::fprintf(stderr, "body=%s lines=%zu error=%s\n", mock.response_body.constData(), lines.size(), error.c_str());
			return Fail("gemini vision regions were not parsed");
		}
		std::printf("PASS gemini vision OCR + translation\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server did not listen");
		mock.response_body = R"({"ParsedResults":[{"ParsedText":"สวัสดี\r\nโลก"}]})";
		CloudOcrEngine engine;
		std::string error;
		trans::OcrConfig config;
		config.cloud_provider = "ocrspace";
		config.cloud_api_key = "helloworld";
		config.cloud_endpoint = mock.url(QStringLiteral("/ocr")).toStdString();
		if (!engine.load(config, &error))
			return Fail("ocr.space load failed");
		const auto lines = engine.recognize(TestImage(), &error);
		if (lines.size() != 2 || lines[0].text != "สวัสดี" || lines[1].text != "โลก")
			return Fail("ocr.space text was not parsed");
		std::printf("PASS ocr.space cloud OCR\n");
	}

	{
		// The pipeline calls recognize() from its worker thread. QNetworkAccessManager
		// and QEventLoop must work without the GUI thread.
		MockServer mock;
		if (!mock.start())
			return Fail("mock server did not listen");
		mock.response_body = R"({"responses":[{"fullTextAnnotation":{"text":"worker"}}]})";
		CloudOcrEngine engine;
		std::string load_error;
		trans::OcrConfig config;
		config.cloud_provider = "google-vision";
		config.cloud_api_key = "test-key";
		config.cloud_endpoint = mock.url(QStringLiteral("/worker")).toStdString();
		if (!engine.load(config, &load_error))
			return Fail("worker cloud OCR load failed");
		std::vector<trans::OcrLine> lines;
		std::string error;
		std::atomic<bool> done{false};
		std::thread worker([&]() {
			lines = engine.recognize(TestImage(), &error);
			done = true;
		});
		while (!done)
		{
			QCoreApplication::processEvents();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		worker.join();
		if (lines.size() != 1 || lines[0].text != "worker")
			return Fail("cloud OCR failed on a worker thread");
		std::printf("PASS cloud OCR worker thread\n");
	}

	{
		CloudOcrEngine engine;
		std::string error;
		trans::OcrConfig config;
		config.cloud_provider = "google-vision";
		if (engine.load(config, &error))
			return Fail("google vision accepted a missing key");
		std::printf("PASS cloud OCR rejects missing key\n");
	}

	return 0;
}
