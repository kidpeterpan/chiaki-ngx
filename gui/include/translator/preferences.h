// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Pure translator preference rules, shared by the Qt settings adapter and the tests.
#pragma once

namespace translator {

int clampOverlayAutoHideSeconds(int seconds);
int clampScanIntervalMs(int milliseconds);
int clampStableMs(int milliseconds);
int clampOverlayFontSize(int pixels);

// Backend to use when none was ever saved. The keyless Google Translate backend lets
// a first run work without typing a key; an install that saved a Gemini key under the
// old implicit Gemini default keeps Gemini.
const char *defaultBackendId(bool has_gemini_key);

} // namespace translator
