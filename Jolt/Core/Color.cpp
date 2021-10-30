// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt.h>

#include <Core/Color.h>

namespace JPH {

// Predefined colors
const Color Color::sBlack(0, 0, 0);
const Color Color::sDarkRed(128, 0, 0);
const Color Color::sRed(255, 0, 0);
const Color Color::sDarkGreen(0, 128, 0);
const Color Color::sGreen(0, 255, 0);
const Color Color::sDarkBlue(0, 0, 128);
const Color Color::sBlue(0, 0, 255);
const Color Color::sYellow(255, 255, 0);
const Color Color::sPurple(255, 0, 255);
const Color Color::sCyan(0, 255, 255);
const Color Color::sOrange(255, 128, 0);
const Color Color::sDarkOrange(128, 64, 0);
const Color Color::sGrey(128, 128, 128);
const Color Color::sLightGrey(192, 192, 192);
const Color Color::sWhite(255, 255, 255);

// Generated by: http://phrogz.net/css/distinct-colors.html (this algo: https://en.wikipedia.org/wiki/Color_difference#CMC_l:c_.281984.29)
static const Color sColors[] = { Color(255, 0, 0), Color(204, 143, 102), Color(226, 242, 0), Color(41, 166, 124), Color(0, 170, 255), Color(69, 38, 153), Color(153, 38, 130), Color(229, 57, 80), Color(204, 0, 0), Color(255, 170, 0), Color(85, 128, 0), Color(64, 255, 217), Color(0, 75, 140), Color(161, 115, 230), Color(242, 61, 157), Color(178, 101, 89), Color(140, 94, 0), Color(181, 217, 108), Color(64, 242, 255), Color(77, 117, 153), Color(157, 61, 242), Color(140, 0, 56), Color(127, 57, 32), Color(204, 173, 51), Color(64, 255, 64), Color(38, 145, 153), Color(0, 102, 255), Color(242, 0, 226), Color(153, 77, 107), Color(229, 92, 0), Color(140, 126, 70), Color(0, 179, 71), Color(0, 194, 242), Color(27, 0, 204), Color(230, 115, 222), Color(127, 0, 17) };

Color Color::sGetDistinctColor(int inIndex)
{
	JPH_ASSERT(inIndex >= 0);

	return sColors[inIndex % (sizeof(sColors) / sizeof(uint32))];
}

} // JPH