// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Local HTTP mock tests for the Gemini/DeepL Qt backends. No credentials or
// external network calls are used.
#include "translator/cloudbackends.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace {

struct MockResponse
{
	int status = 200;
	QByteArray body = "{}";
	bool respond = true;
};

struct MockServer
{
	QTcpServer server;
	MockResponse response;
	QByteArray last_request;
	QString last_path;

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
					last_request = *buffer;
					const QList<QByteArray> request_line = buffer->left(header_end).split('\r')[0].split(' ');
					if (request_line.size() >= 2)
						last_path = QString::fromUtf8(request_line[1]);
					if (!response.respond)
						return;
					const QByteArray header = "HTTP/1.1 " + QByteArray::number(response.status) +
						" Mock\r\nContent-Type: application/json\r\nContent-Length: " +
						QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n\r\n";
					socket->write(header + response.body);
					socket->flush();
					socket->disconnectFromHost();
				});
			}
		});
		return server.listen(QHostAddress::LocalHost, 0);
	}

	QString base_url() const
	{
		return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
	}
};

bool WaitFor(std::function<bool()> predicate, int timeout_ms = 5000)
{
	QEventLoop loop;
	QTimer timer;
	timer.setSingleShot(true);
	QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
	timer.start(timeout_ms);
	while (!predicate())
	{
		QTimer::singleShot(10, &loop, &QEventLoop::quit);
		loop.exec();
		if (timer.isActive() == false)
			break;
	}
	return predicate();
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
	int failures = 0;

	auto run_translation = [&](CloudBackendOptions options, const trans::TranslationRequest &request,
		trans::TranslationResult *out, int timeout_ms = 5000) {
		CloudTranslatorBackend backend(std::move(options));
		bool done = false;
		backend.translate(request, [&](trans::TranslationResult result) {
			*out = std::move(result);
			done = true;
		});
		WaitFor([&]() { return done; }, timeout_ms);
		return done;
	};

	// Gemini success + request shape.
	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"candidates":[{"content":{"parts":[{"text":"สวัสดี"}]}}]})";
		CloudBackendOptions options;
		options.provider = "gemini";
		options.api_key = "test-key";
		options.model = "test-model";
		options.gemini_base_url = QUrl(mock.base_url() + "/v1beta/models/");
		trans::TranslationRequest request;
		request.text = "こんにちは";
		request.source_language = "ja";
		request.target_language = "th";
		trans::TranslationResult result;
		if (!run_translation(options, request, &result))
			return Fail("gemini callback not invoked");
		if (!result.ok || result.text != "สวัสดี")
			return Fail("gemini response was not parsed");
		if (!mock.last_path.contains(QStringLiteral("/v1beta/models/test-model:generateContent")))
			return Fail("gemini URL did not contain the model path");
		if (!mock.last_request.contains("test-key"))
			return Fail("gemini request did not include the API key query");
		if (!mock.last_request.contains(QString::fromUtf8("こんにちは").toUtf8()))
			return Fail("gemini request did not include the subtitle text");
		std::printf("PASS gemini parse\n");
	}

	// The pipeline calls translate() from its worker thread; the Qt network
	// work must still be marshalled onto the GUI thread.
	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"candidates":[{"content":{"parts":[{"text":"เธอ"}]}}]})";
		CloudBackendOptions options;
		options.provider = "gemini";
		options.api_key = "thread-key";
		options.gemini_base_url = QUrl(mock.base_url() + "/v1beta/models/");
		CloudTranslatorBackend backend(options);
		trans::TranslationRequest request;
		request.text = "あなた";
		trans::TranslationResult result;
		bool done = false;
		std::thread worker([&]() {
			backend.translate(request, [&](trans::TranslationResult translated) {
				result = std::move(translated);
				done = true;
			});
		});
		worker.join();
		if (!WaitFor([&]() { return done; }))
			return Fail("worker-thread callback not invoked");
		if (!result.ok || result.text.empty())
			return Fail("worker-thread translation failed");
		std::printf("PASS worker-thread dispatch\n");
	}

	// DeepL success.
	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"translations":[{"text":"สวัสดี"}]})";
		CloudBackendOptions options;
		options.provider = "deepl";
		options.api_key = "deepl-key";
		options.deepl_endpoint = QUrl(mock.base_url() + "/v2/translate");
		trans::TranslationRequest request;
		request.text = "こんにちは";
		request.target_language = "th";
		trans::TranslationResult result;
		if (!run_translation(options, request, &result))
			return Fail("deepl callback not invoked");
		if (!result.ok || result.text != "สวัสดี")
			return Fail("deepl response was not parsed");
		if (!mock.last_request.contains("DeepL-Auth-Key deepl-key"))
			return Fail("deepl authorization header missing");
		std::printf("PASS deepl parse\n");
	}

	// HTTP error surface.
	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.status = 400;
		mock.response.body = R"({"error":"bad request"})";
		CloudBackendOptions options;
		options.provider = "gemini";
		options.api_key = "test-key";
		options.gemini_base_url = QUrl(mock.base_url() + "/v1beta/models/");
		trans::TranslationRequest request;
		request.text = "test";
		trans::TranslationResult result;
		if (!run_translation(options, request, &result))
			return Fail("error callback not invoked");
		if (result.ok || result.error.find("400") == std::string::npos)
			return Fail("HTTP 400 was not surfaced");
		std::printf("PASS http error\n");
	}

	// Timeout.
	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.respond = false;
		CloudBackendOptions options;
		options.provider = "gemini";
		options.api_key = "test-key";
		options.gemini_base_url = QUrl(mock.base_url() + "/v1beta/models/");
		options.timeout_ms = 150;
		trans::TranslationRequest request;
		request.text = "test";
		trans::TranslationResult result;
		if (!run_translation(options, request, &result, 3000))
			return Fail("timeout callback not invoked");
		if (result.ok || result.error.find("timed out") == std::string::npos)
			return Fail("timeout was not surfaced");
		std::printf("PASS timeout\n");
	}

	std::printf("%d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
