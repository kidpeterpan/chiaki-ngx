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

	std::printf("PASS overlay geometry mapping and overlap resolution\n");
	return 0;
}
