// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
#include "translator/overlaygeometry.h"

#include <cstdio>

int main()
{
	const translator::OverlayRect mapped = translator::mapBoxToViewport(
		trans::Rect{100, 50, 200, 100}, 1000, 500, 0.1, 0.2, 0.8, 0.6);
	const double epsilon = 1e-9;
	if (std::abs(mapped.x - 0.18) > epsilon || std::abs(mapped.y - 0.26) > epsilon ||
			std::abs(mapped.width - 0.16) > epsilon || std::abs(mapped.height - 0.12) > epsilon)
	{
		std::fprintf(stderr, "box mapping failed: %f %f %f %f\n",
			mapped.x, mapped.y, mapped.width, mapped.height);
		return 1;
	}

	const translator::OverlayRect clamped = translator::mapBoxToViewport(
		trans::Rect{900, 450, 200, 100}, 1000, 500, 0.0, 0.0, 1.0, 1.0);
	if (clamped.x + clamped.width > 1.0 + epsilon || clamped.y + clamped.height > 1.0 + epsilon)
	{
		std::fprintf(stderr, "box mapping was not clamped\n");
		return 1;
	}

	std::vector<translator::OverlayRect> overlapping{
		{0.1, 0.1, 0.5, 0.2},
		{0.1, 0.15, 0.5, 0.2},
	};
	translator::resolveOverlayOverlaps(overlapping);
	if (overlapping[1].y < overlapping[0].y + overlapping[0].height)
	{
		std::fprintf(stderr, "overlap was not pushed down\n");
		return 1;
	}

	std::vector<translator::OverlayRect> side_by_side{
		{0.1, 0.1, 0.2, 0.2},
		{0.6, 0.1, 0.2, 0.2},
	};
	translator::resolveOverlayOverlaps(side_by_side);
	if (std::abs(side_by_side[1].y - 0.1) > epsilon)
	{
		std::fprintf(stderr, "side-by-side boxes were moved\n");
		return 1;
	}

	const translator::OverlayRect letterbox = translator::videoContentRect(16.0 / 9.0, 4.0 / 3.0, 0, -1.0);
	if (std::abs(letterbox.height - 0.75) > epsilon || std::abs(letterbox.y - 0.125) > epsilon)
	{
		std::fprintf(stderr, "letterbox mapping failed\n");
		return 1;
	}
	const translator::OverlayRect pillarbox = translator::videoContentRect(4.0 / 3.0, 16.0 / 9.0, 0, -1.0);
	if (std::abs(pillarbox.width - 0.75) > epsilon || std::abs(pillarbox.x - 0.125) > epsilon)
	{
		std::fprintf(stderr, "pillarbox mapping failed\n");
		return 1;
	}
	const translator::OverlayRect stretch = translator::videoContentRect(4.0 / 3.0, 16.0 / 9.0, 1, -1.0);
	if (stretch.x != 0.0 || stretch.y != 0.0 || stretch.width != 1.0 || stretch.height != 1.0)
	{
		std::fprintf(stderr, "stretch mapping failed\n");
		return 1;
	}
	const translator::OverlayRect cover = translator::videoContentRect(4.0 / 3.0, 16.0 / 9.0, 2, -1.0);
	if (cover.width < 1.0 || cover.x > 0.0)
	{
		std::fprintf(stderr, "zoom-cover mapping failed\n");
		return 1;
	}
	const translator::OverlayRect zoomed = translator::videoContentRect(16.0 / 9.0, 16.0 / 9.0, 2, 1.0);
	if (std::abs(zoomed.width - 2.0) > epsilon || std::abs(zoomed.x + 0.5) > epsilon)
	{
		std::fprintf(stderr, "positive zoom mapping failed\n");
		return 1;
	}
	std::printf("PASS overlay geometry mapping, letterbox/zoom and overlap resolution\n");
	return 0;
}
