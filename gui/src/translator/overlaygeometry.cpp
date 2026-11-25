// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/overlaygeometry.h"

#include <algorithm>

namespace translator {

namespace {
double clamp01(double value)
{
	return std::max(0.0, std::min(1.0, value));
}
} // namespace

OverlayRect mapBoxToViewport(const trans::Rect &box, int source_width, int source_height,
		double roi_x, double roi_y, double roi_width, double roi_height)
{
	OverlayRect result;
	if (source_width <= 0 || source_height <= 0)
	{
		result = {roi_x, roi_y, roi_width, roi_height};
	}
	else
	{
		result.x = roi_x + (static_cast<double>(box.x) / source_width) * roi_width;
		result.y = roi_y + (static_cast<double>(box.y) / source_height) * roi_height;
		result.width = (static_cast<double>(box.width) / source_width) * roi_width;
		result.height = (static_cast<double>(box.height) / source_height) * roi_height;
	}
	result.x = clamp01(result.x);
	result.y = clamp01(result.y);
	result.width = std::max(0.01, std::min(result.width, 1.0 - result.x));
	result.height = std::max(0.01, std::min(result.height, 1.0 - result.y));
	return result;
}

void resolveOverlayOverlaps(std::vector<OverlayRect> &rects)
{
	for (size_t index = 1; index < rects.size(); ++index)
	{
		OverlayRect &current = rects[index];
		for (size_t previous = 0; previous < index; ++previous)
		{
			const OverlayRect &earlier = rects[previous];
			const bool x_overlap = current.x < earlier.x + earlier.width &&
				current.x + current.width > earlier.x;
			const bool y_overlap = current.y < earlier.y + earlier.height &&
				current.y + current.height > earlier.y;
			if (x_overlap && y_overlap)
			{
				current.y = std::max(0.0, std::min(1.0 - current.height,
					earlier.y + earlier.height + 0.005));
			}
		}
	}
}

} // namespace translator
