// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Local HTTP mock tests for the Gemini/DeepL Qt backends. No credentials or
// external network calls are used.
#include "translator/cloudbackends.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

	// OpenAI-compatible chat completions (providers "openai" and "opencode-go").
	auto openai_options = [](const MockServer &mock, const QString &base_suffix = QStringLiteral("/v1"),
		const QString &key = QStringLiteral("k1")) {
		CloudBackendOptions options;
		options.provider = "openai";
		options.api_key = key;
		options.model = "m1";
		options.openai_base_url = QUrl(mock.base_url() + base_suffix);
		return options;
	};
	auto japanese_request = []() {
		trans::TranslationRequest request;
		request.text = "こんにちは";
		request.source_language = "ja";
		request.target_language = "th";
		return request;
	};

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"choices":[{"message":{"content":"สวัสดี"}}]})";
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock), japanese_request(), &result))
			return Fail("openai callback not invoked");
		if (!result.ok || result.text != "สวัสดี")
		{
			std::fprintf(stderr, "openai result: ok=%d text=\"%s\" error=\"%s\"\n", result.ok,
				result.text.c_str(), result.error.c_str());
			return Fail("openai response was not parsed");
		}
		if (!mock.last_path.endsWith(QStringLiteral("/v1/chat/completions")))
			return Fail("openai URL did not end with /v1/chat/completions");
		const QByteArray lowered = mock.last_request.toLower();
		if (!lowered.contains("authorization: bearer k1"))
			return Fail("openai bearer header missing");
		if (!mock.last_request.contains("\"model\":\"m1\""))
			return Fail("openai request did not carry the model");
		if (!mock.last_request.contains("\"temperature\":0.2"))
			return Fail("openai request did not carry the temperature");
		if (!mock.last_request.contains(QString::fromUtf8("こんにちは").toUtf8()))
			return Fail("openai request did not include the subtitle text");
		if (!mock.last_request.contains("\"role\":\"system\"") || !mock.last_request.contains("\"role\":\"user\""))
			return Fail("openai request lacked system and user messages");
		std::printf("PASS openai parse\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"choices":[{"message":{"content":"ok"}}]})";
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock, QStringLiteral("/v1/")), japanese_request(), &result))
			return Fail("openai trailing-slash callback not invoked");
		if (mock.last_path != QStringLiteral("/v1/chat/completions"))
			return Fail("trailing slash produced a malformed path");
		std::printf("PASS openai base url trailing slash\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"choices":[{"message":{"content":"ok"}}]})";
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock, QStringLiteral("/v1"), QString()), japanese_request(), &result))
			return Fail("openai keyless callback not invoked");
		if (!result.ok)
			return Fail("openai keyless localhost request failed");
		if (mock.last_request.toLower().contains("authorization"))
			return Fail("keyless request still sent an Authorization header");
		std::printf("PASS openai no key no auth header\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"choices":[{"message":{"content":"ok"}}]})";
		trans::TranslationRequest request = japanese_request();
		const QString tricky = QString::fromUtf8("He said \"hi\"\nbye");
		request.text = tricky.toStdString();
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock), request, &result))
			return Fail("openai escape callback not invoked");
		const int body_start = mock.last_request.indexOf("\r\n\r\n") + 4;
		QJsonParseError parse_error;
		const QJsonDocument document = QJsonDocument::fromJson(mock.last_request.mid(body_start), &parse_error);
		if (parse_error.error != QJsonParseError::NoError)
			return Fail("openai request body was not valid JSON");
		bool found = false;
		for (const QJsonValue &message : document.object().value(QStringLiteral("messages")).toArray())
			if (message.toObject().value(QStringLiteral("role")).toString() == QStringLiteral("user") &&
				message.toObject().value(QStringLiteral("content")).toString().contains(tricky))
				found = true;
		if (!found)
			return Fail("user message lost the quotes or newline");
		std::printf("PASS openai escapes quotes and newlines\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		const QByteArray bodies[] = {
			R"({"choices":[]})",
			R"({"choices":[{"message":{"content":null}}]})",
		};
		for (const QByteArray &body : bodies)
		{
			mock.response.body = body;
			trans::TranslationResult result;
			if (!run_translation(openai_options(mock), japanese_request(), &result))
				return Fail("openai empty-content callback not invoked");
			if (result.ok || result.error.find("empty") == std::string::npos)
				return Fail("empty openai content was not reported as empty");
		}
		std::printf("PASS openai empty content\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.body = R"({"choices":[{"message":{"content":"","reasoning_content":"thinking"}}]})";
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock), japanese_request(), &result))
			return Fail("openai reasoning callback not invoked");
		if (result.ok || result.error.find("no content") == std::string::npos)
			return Fail("reasoning-only reply was not reported");
		std::printf("PASS openai reasoning only\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.status = 401;
		mock.response.body = R"({"error":"invalid key k1"})";
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock), japanese_request(), &result))
			return Fail("openai 401 callback not invoked");
		if (result.ok || result.error.find("401") == std::string::npos)
			return Fail("openai HTTP 401 was not surfaced");
		if (result.error.find("k1") != std::string::npos)
			return Fail("openai error text leaked the API key");
		std::printf("PASS openai http error redacts key\n");
	}

	// The error body is shortened for display. A key that straddles the cut must not
	// leave a recognizable prefix behind, so redaction has to happen before shortening.
	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		const QString long_key = QStringLiteral("SECRET-") + QString(300, QLatin1Char('x'));
		mock.response.status = 401;
		mock.response.body = (QStringLiteral("{\"error\":\"") + QString(200, QLatin1Char('.')) + long_key +
			QStringLiteral("\"}")).toUtf8();
		trans::TranslationResult result;
		if (!run_translation(openai_options(mock, QStringLiteral("/v1"), long_key), japanese_request(), &result))
			return Fail("openai long-key callback not invoked");
		if (result.ok || result.error.find("401") == std::string::npos)
			return Fail("openai HTTP 401 with a long key was not surfaced");
		if (result.error.find("SECRET") != std::string::npos)
			return Fail("a truncated prefix of the API key survived redaction");
		std::printf("PASS openai redacts a key cut by truncation\n");
	}

	{
		MockServer mock;
		if (!mock.start())
			return Fail("mock server failed to listen");
		mock.response.respond = false;
		CloudBackendOptions options = openai_options(mock);
		options.timeout_ms = 150;
		trans::TranslationResult result;
		if (!run_translation(options, japanese_request(), &result, 3000))
			return Fail("openai timeout callback not invoked");
		if (result.ok || result.error.find("timed out") == std::string::npos)
			return Fail("openai timeout was not surfaced");
		std::printf("PASS openai timeout\n");
	}

	{
		CloudBackendOptions go;
		go.provider = "opencode-go";
		if (CloudTranslatorBackend::effectiveOpenAiBaseUrl(go) !=
			QUrl(QStringLiteral("https://opencode.ai/inference/go/openai/v1")))
			return Fail("opencode-go default base URL is wrong");
		CloudBackendOptions generic;
		generic.provider = "openai";
		if (!CloudTranslatorBackend::effectiveOpenAiBaseUrl(generic).isEmpty())
			return Fail("openai must have no default base URL");
		std::printf("PASS opencode-go default base url\n");
	}

	{
		struct Row
		{
			const char *provider;
			const char *base;
			const char *model;
			const char *key;
			const char *expected;
		};
		const Row rows[] = {
			{"opencode-go", "", "m", "", "Add a opencode-go API key in translator settings"},
			{"opencode-go", "", "m", "k", ""},
			{"openai", "", "m", "k", "Set a base URL for the OpenAI-compatible backend"},
			{"openai", "http://example.com/v1", "m", "k", "Base URL must use https (http is allowed only for localhost)"},
			{"openai", "http://localhost:11434/v1", "m", "", ""},
			{"openai", "http://[::1]:1234/v1", "m", "", ""},
			{"openai", "http://127.0.0.1:9/v1", "m", "", ""},
			{"openai", "https://x/v1", "", "k", "Set a model name"},
			{"openai", "https://x/v1", "m", "", "Add a openai API key in translator settings"},
			{"gemini", "", "", "", ""},
		};
		for (const Row &row : rows)
		{
			CloudBackendOptions options;
			options.provider = row.provider;
			options.openai_base_url = QUrl(QString::fromUtf8(row.base));
			options.model = row.model;
			options.api_key = row.key;
			const QString actual = CloudTranslatorBackend::validateOptions(options);
			if (actual != QString::fromUtf8(row.expected))
			{
				std::fprintf(stderr, "validateOptions(%s, %s, %s, %s) returned \"%s\"\n", row.provider,
					row.base, row.model, row.key, actual.toUtf8().constData());
				return Fail("validateOptions table mismatch");
			}
		}
		std::printf("PASS validate options\n");
	}

	{
		CloudBackendOptions options;
		options.provider = "openai";
		options.api_key = "k1";
		options.model = "m1";
		options.openai_base_url = QUrl(QStringLiteral("http://example.com/v1"));
		trans::TranslationResult result;
		if (!run_translation(options, japanese_request(), &result, 1000))
			return Fail("non-local http callback not invoked");
		if (result.ok || result.error.find("https") == std::string::npos)
			return Fail("non-local http base URL was not refused");
		std::printf("PASS openai refuses http non-local at request time\n");
	}

	std::printf("%d failure(s)\n", failures);
	return failures == 0 ? 0 : 1;
}
