// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Pure clamps for translator timing preferences, shared by the Qt settings
// adapter and the tests.
#pragma once

namespace translator {

int clampOverlayAutoHideSeconds(int seconds);
int clampScanIntervalMs(int milliseconds);
int clampStableMs(int milliseconds);
int clampOverlayFontSize(int pixels);

} // namespace translator
