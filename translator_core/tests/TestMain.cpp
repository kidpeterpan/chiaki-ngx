// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "TestHarness.hpp"

#include <cstring>

namespace tst {

std::vector<Case> &Registry()
{
	static std::vector<Case> cases;
	return cases;
}

} // namespace tst

int main(int argc, char **argv)
{
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int failures = 0;
	int executed = 0;
	for (const tst::Case &test : tst::Registry())
	{
		if (filter && std::strstr(test.name, filter) == nullptr)
			continue;
		++executed;
		try
		{
			test.fn();
			std::printf("PASS  %s\n", test.name);
		}
		catch (const std::exception &e)
		{
			++failures;
			std::printf("FAIL  %s\n      %s\n", test.name, e.what());
		}
	}
	std::printf("\n%d test(s), %d failure(s)\n", executed, failures);
	return failures == 0 ? 0 : 1;
}
