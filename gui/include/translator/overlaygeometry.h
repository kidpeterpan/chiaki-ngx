// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// Pure geometry used by the box-accurate overlay: ROI-crop coordinates to
// normalized viewport coordinates, plus collision nudging.
#pragma once

#include <trans/Types.hpp>

#include <vector>

namespace translator {

struct OverlayRect
{
	double x = 0.0;
	double y = 0.0;
	double width = 1.0;
	double height = 1.0;
};

OverlayRect mapBoxToViewport(const trans::Rect &box, int source_width, int source_height,
		double roi_x, double roi_y, double roi_width, double roi_height);

// Pushes later overlapping blocks down inside the viewport. Side-by-side
// boxes do not overlap horizontally and are left untouched.
void resolveOverlayOverlaps(std::vector<OverlayRect> &rects);

// Maps the decoded video frame into the window. mode: 0=normal, 1=stretch,
// 2=zoom. zoom_factor -1 means cover; otherwise base-2 zoom.
OverlayRect videoContentRect(double frame_aspect, double window_aspect, int mode,
		double zoom_factor);

} // namespace translator
