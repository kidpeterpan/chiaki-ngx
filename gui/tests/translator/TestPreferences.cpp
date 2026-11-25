// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/preferences.h"

#include <cstdio>

int main()
{
	if (translator::clampOverlayAutoHideSeconds(-1) != 0 ||
			translator::clampOverlayAutoHideSeconds(0) != 0 ||
			translator::clampOverlayAutoHideSeconds(6) != 6 ||
			translator::clampOverlayAutoHideSeconds(999) != 60)
	{
		std::fprintf(stderr, "overlay auto-hide clamp failed\n");
		return 1;
	}
	if (translator::clampScanIntervalMs(1) != 50 ||
			translator::clampScanIntervalMs(100) != 100 ||
			translator::clampScanIntervalMs(10000) != 2000)
	{
		std::fprintf(stderr, "scan interval clamp failed\n");
		return 1;
	}
	if (translator::clampStableMs(1) != 100 ||
			translator::clampStableMs(250) != 250 ||
			translator::clampStableMs(10000) != 5000)
	{
		std::fprintf(stderr, "stable time clamp failed\n");
		return 1;
	}
	std::printf("PASS timing preference clamps\n");
	return 0;
}
