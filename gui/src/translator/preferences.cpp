// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/preferences.h"


namespace translator {

int clampOverlayAutoHideSeconds(int seconds)
{
	return seconds < 0 ? 0 : (seconds > 60 ? 60 : seconds);
}

int clampScanIntervalMs(int milliseconds)
{
	return milliseconds < 50 ? 50 : (milliseconds > 2000 ? 2000 : milliseconds);
}

int clampStableMs(int milliseconds)
{
	return milliseconds < 100 ? 100 : (milliseconds > 5000 ? 5000 : milliseconds);
}

int clampOverlayFontSize(int pixels)
{
	return pixels < 0 ? 0 : (pixels > 72 ? 72 : pixels);
}

int clampOverlayBoxOpacity(int percent)
{
	return percent < 20 ? 20 : (percent > 100 ? 100 : percent);
}

const char *defaultBackendId(bool has_gemini_key)
{
	return has_gemini_key ? "gemini" : "google";
}

} // namespace translator
