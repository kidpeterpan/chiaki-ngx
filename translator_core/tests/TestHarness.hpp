// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Minimal test harness for translator_core. No external dependencies so the
// module can be built on a SteamOS image without a package manager.
#pragma once

#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace tst {

struct Case
{
	const char *name;
	void (*fn)();
};

std::vector<Case> &Registry();

struct Registrar
{
	Registrar(const char *name, void (*fn)()) { Registry().push_back({name, fn}); }
};

class Failure : public std::exception
{
public:
	explicit Failure(std::string message) : message_(std::move(message)) {}
	const char *what() const noexcept override { return message_.c_str(); }

private:
	std::string message_;
};

inline std::string Describe(const std::string &expression, const char *file, int line)
{
	std::ostringstream out;
	out << file << ":" << line << ": " << expression;
	return out.str();
}

} // namespace tst

#define TEST(name)                                                   \
	static void test_##name();                                       \
	static ::tst::Registrar registrar_##name(#name, test_##name);     \
	static void test_##name()

#define FAIL_AT(expression) throw ::tst::Failure(::tst::Describe(expression, __FILE__, __LINE__))

#define ASSERT_TRUE(condition)                        \
	do                                               \
	{                                                \
		if (!(condition))                            \
			FAIL_AT("ASSERT_TRUE(" #condition ")");  \
	} while (false)

#define ASSERT_FALSE(condition) ASSERT_TRUE(!(condition))

#define ASSERT_EQ(actual, expected)                                            \
	do                                                                         \
	{                                                                          \
		const auto &actual_value = (actual);                                   \
		const auto &expected_value = (expected);                               \
		if (!(actual_value == expected_value))                                 \
		{                                                                      \
			std::ostringstream failure_stream;                                 \
			failure_stream << #actual " == " #expected " (got " << actual_value \
						   << ", want " << expected_value << ")";             \
			throw ::tst::Failure(                                              \
				::tst::Describe(failure_stream.str(), __FILE__, __LINE__));    \
		}                                                                      \
	} while (false)

#define ASSERT_NE(actual, unexpected) ASSERT_TRUE((actual) != (unexpected))

#define ASSERT_NEAR(actual, expected, tolerance)                                     \
	do                                                                               \
	{                                                                                \
		const double actual_value = (actual);                                        \
		const double expected_value = (expected);                                    \
		const double tolerance_value = (tolerance);                                  \
		if (std::fabs(actual_value - expected_value) > tolerance_value)               \
		{                                                                            \
			std::ostringstream failure_stream;                                       \
			failure_stream << #actual " ~= " #expected " (got " << actual_value      \
						   << ", want " << expected_value << ")";                   \
			throw ::tst::Failure(                                                    \
				::tst::Describe(failure_stream.str(), __FILE__, __LINE__));          \
		}                                                                            \
	} while (false)
