// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/preferences.h"

#include <cstdio>
#include <cstring>

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
	if (translator::clampOverlayFontSize(-1) != 0 ||
			translator::clampOverlayFontSize(0) != 0 ||
			translator::clampOverlayFontSize(22) != 22 ||
			translator::clampOverlayFontSize(999) != 72)
	{
		std::fprintf(stderr, "overlay font size clamp failed\n");
		return 1;
	}
	std::printf("PASS timing preference clamps\n");

	if (translator::clampOverlayBoxOpacity(0) != 20 ||
			translator::clampOverlayBoxOpacity(80) != 80 ||
			translator::clampOverlayBoxOpacity(150) != 100)
	{
		std::fprintf(stderr, "overlay box opacity clamp failed\n");
		return 1;
	}
	std::printf("PASS overlay box opacity clamp\n");

	// New installs default to the keyless backend; an install that relied on the old
	// implicit Gemini default and saved a Gemini key keeps Gemini.
	if (std::strcmp(translator::defaultBackendId(false), "google") != 0 ||
			std::strcmp(translator::defaultBackendId(true), "gemini") != 0)
	{
		std::fprintf(stderr, "default backend selection failed\n");
		return 1;
	}
	std::printf("PASS default backend selection\n");
	return 0;
}
