/*
 * Colors for qemacs.
 *
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2002-2022 Charlie Gordon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <limits.h>

#include "util.h"
#include "color.h"

#if 1
/* Should move all this to a separate source file color.c */

/* For 8K colors, We use a color system with 7936 colors:
 *   - 16 standard colors
 *   - 240 standard palette colors
 *   - 4096 colors in a 16x16x16 cube
 *   - a 256 level gray ramp
 *   - 6 256-level fade to black ramps
 *   - 6 256-level fade to white ramps
 *   - a 256 color palette with default xterm values
 *   - 256 unused slots
 */

/* Alternately we could use a system with 8157 colors:
 *   - 2 default color
 *   - 16 standard colors
 *   - 256 standard palette colors
 *   - 6859 colors in a 19x19x19 cube
 *     with ramp 0,15,31,47,63,79,95,108,121,135,
 *               148,161,175,188,201,215,228,241,255 values
 *   - 256 level gray ramp
 *   - extra space for 3 256 level ramps or 12 64 level ramps
 *   - 15 unused slots
 */

/* Another possible system for 8K colors has 8042+ colors:
 *   - 2 default color
 *   - 16 standard colors
 *   - 24 standard grey scale colors
 *   - 8000 colors in a 20x20x20 cube
 *     with ramp 0,13,27,40,54,67,81,95,108,121,135,
 *               148,161,175,188,201,215,228,241,255 values
 *   - extra grey scale colors
 *   - some unused slots
 */

static ColorDef const default_colors[] = {
    /* From HTML 4.0 spec */
    { "black",   QERGB(0x00, 0x00, 0x00) },
    { "green",   QERGB(0x00, 0x80, 0x00) },
    { "silver",  QERGB(0xc0, 0xc0, 0xc0) },
    { "lime",    QERGB(0x00, 0xff, 0x00) },

    { "gray",    QERGB(0xbe, 0xbe, 0xbe) },
    { "olive",   QERGB(0x80, 0x80, 0x00) },
    { "white",   QERGB(0xff, 0xff, 0xff) },
    { "yellow",  QERGB(0xff, 0xff, 0x00) },

    { "maroon",  QERGB(0x80, 0x00, 0x00) },
    { "navy",    QERGB(0x00, 0x00, 0x80) },
    { "red",     QERGB(0xff, 0x00, 0x00) },
    { "blue",    QERGB(0x00, 0x00, 0xff) },

    { "purple",  QERGB(0x80, 0x00, 0x80) },
    { "teal",    QERGB(0x00, 0x80, 0x80) },
    { "fuchsia", QERGB(0xff, 0x00, 0xff) },
    { "aqua",    QERGB(0x00, 0xff, 0xff) },

    /* more colors */
    { "cyan",    QERGB(0x00, 0xff, 0xff) },
    { "magenta", QERGB(0xff, 0x00, 0xff) },
    { "grey",    QERGB(0xbe, 0xbe, 0xbe) },
    { "transparent", COLOR_TRANSPARENT },
};
#define nb_default_colors  countof(default_colors)

ColorDef *qe_colors = unconst(ColorDef *)default_colors;
int nb_qe_colors = nb_default_colors;

#if 0
static QEColor const tty_full_colors[8] = {
    QERGB(0x00, 0x00, 0x00),  /* black */
    QERGB(0xff, 0x00, 0x00),  /* red */
    QERGB(0x00, 0xff, 0x00),  /* lime */
    QERGB(0xff, 0xff, 0x00),  /* yellow */
    QERGB(0x00, 0x00, 0xff),  /* blue */
    QERGB(0xff, 0x00, 0xff),  /* fuchsia */
    QERGB(0x00, 0xff, 0xff),  /* aqua */
    QERGB(0xff, 0xff, 0xff),  /* white */
};
#endif

QEColor const xterm_colors[256] = {
    QERGB(0x00, 0x00, 0x00), /*	black */
    QERGB(0xbb, 0x00, 0x00),
    QERGB(0x00, 0xbb, 0x00),
    QERGB(0xbb, 0xbb, 0x00),
    QERGB(0x00, 0x00, 0xbb),
    QERGB(0xbb, 0x00, 0xbb),
    QERGB(0x00, 0xbb, 0xbb),
    QERGB(0xbb, 0xbb, 0xbb),

    QERGB(0x55, 0x55, 0x55),
    QERGB(0xff, 0x55, 0x55),
    QERGB(0x55, 0xff, 0x55),
    QERGB(0xff, 0xff, 0x55),
    QERGB(0x55, 0x55, 0xff),
    QERGB(0xff, 0x55, 0xff),
    QERGB(0x55, 0xff, 0xff),
    QERGB(0xff, 0xff, 0xff), /*	white */
#if 1
    /* Extended color palette for xterm 256 color mode */

    /* From XFree86: xc/programs/xterm/256colres.h,
     * v 1.5 2002/10/05 17:57:11 dickey Exp
     */

    /* 216 entry RGB cube with axes 0,95,135,175,215,255 */
    /* followed by 24 entry grey scale 8,18..238 */
    QERGB(0x00, 0x00, 0x00),  /* 16: Grey0 */
    QERGB(0x00, 0x00, 0x5f),  /* 17: NavyBlue */
    QERGB(0x00, 0x00, 0x87),  /* 18: DarkBlue */
    QERGB(0x00, 0x00, 0xaf),  /* 19: Blue3 */
    QERGB(0x00, 0x00, 0xd7),  /* 20: Blue3 */
    QERGB(0x00, 0x00, 0xff),  /* 21: Blue1 */
    QERGB(0x00, 0x5f, 0x00),  /* 22: DarkGreen */
    QERGB(0x00, 0x5f, 0x5f),  /* 23: DeepSkyBlue4 */
    QERGB(0x00, 0x5f, 0x87),  /* 24: DeepSkyBlue4 */
    QERGB(0x00, 0x5f, 0xaf),  /* 25: DeepSkyBlue4 */
    QERGB(0x00, 0x5f, 0xd7),  /* 26: DodgerBlue3 */
    QERGB(0x00, 0x5f, 0xff),  /* 27: DodgerBlue2 */
    QERGB(0x00, 0x87, 0x00),  /* 28: Green4 */
    QERGB(0x00, 0x87, 0x5f),  /* 29: SpringGreen4 */
    QERGB(0x00, 0x87, 0x87),  /* 30: Turquoise4 */
    QERGB(0x00, 0x87, 0xaf),  /* 31: DeepSkyBlue3 */
    QERGB(0x00, 0x87, 0xd7),  /* 32: DeepSkyBlue3 */
    QERGB(0x00, 0x87, 0xff),  /* 33: DodgerBlue1 */
    QERGB(0x00, 0xaf, 0x00),  /* 34: Green3 */
    QERGB(0x00, 0xaf, 0x5f),  /* 35: SpringGreen3 */
    QERGB(0x00, 0xaf, 0x87),  /* 36: DarkCyan */
    QERGB(0x00, 0xaf, 0xaf),  /* 37: LightSeaGreen */
    QERGB(0x00, 0xaf, 0xd7),  /* 38: DeepSkyBlue2 */
    QERGB(0x00, 0xaf, 0xff),  /* 39: DeepSkyBlue1 */
    QERGB(0x00, 0xd7, 0x00),  /* 40: Green3 */
    QERGB(0x00, 0xd7, 0x5f),  /* 41: SpringGreen3 */
    QERGB(0x00, 0xd7, 0x87),  /* 42: SpringGreen2 */
    QERGB(0x00, 0xd7, 0xaf),  /* 43: Cyan3 */
    QERGB(0x00, 0xd7, 0xd7),  /* 44: DarkTurquoise */
    QERGB(0x00, 0xd7, 0xff),  /* 45: Turquoise2 */
    QERGB(0x00, 0xff, 0x00),  /* 46: Green1 */
    QERGB(0x00, 0xff, 0x5f),  /* 47: SpringGreen2 */
    QERGB(0x00, 0xff, 0x87),  /* 48: SpringGreen1 */
    QERGB(0x00, 0xff, 0xaf),  /* 49: MediumSpringGreen */
    QERGB(0x00, 0xff, 0xd7),  /* 50: Cyan2 */
    QERGB(0x00, 0xff, 0xff),  /* 51: Cyan1 */
    QERGB(0x5f, 0x00, 0x00),  /* 52: DarkRed */
    QERGB(0x5f, 0x00, 0x5f),  /* 53: DeepPink4 */
    QERGB(0x5f, 0x00, 0x87),  /* 54: Purple4 */
    QERGB(0x5f, 0x00, 0xaf),  /* 55: Purple4 */
    QERGB(0x5f, 0x00, 0xd7),  /* 56: Purple3 */
    QERGB(0x5f, 0x00, 0xff),  /* 57: BlueViolet */
    QERGB(0x5f, 0x5f, 0x00),  /* 58: Orange4 */
    QERGB(0x5f, 0x5f, 0x5f),  /* 59: Grey37 */
    QERGB(0x5f, 0x5f, 0x87),  /* 60: MediumPurple4 */
    QERGB(0x5f, 0x5f, 0xaf),  /* 61: SlateBlue3 */
    QERGB(0x5f, 0x5f, 0xd7),  /* 62: SlateBlue3 */
    QERGB(0x5f, 0x5f, 0xff),  /* 63: RoyalBlue1 */
    QERGB(0x5f, 0x87, 0x00),  /* 64: Chartreuse4 */
    QERGB(0x5f, 0x87, 0x5f),  /* 65: DarkSeaGreen4 */
    QERGB(0x5f, 0x87, 0x87),  /* 66: PaleTurquoise4 */
    QERGB(0x5f, 0x87, 0xaf),  /* 67: SteelBlue */
    QERGB(0x5f, 0x87, 0xd7),  /* 68: SteelBlue3 */
    QERGB(0x5f, 0x87, 0xff),  /* 69: CornflowerBlue */
    QERGB(0x5f, 0xaf, 0x00),  /* 70: Chartreuse3 */
    QERGB(0x5f, 0xaf, 0x5f),  /* 71: DarkSeaGreen4 */
    QERGB(0x5f, 0xaf, 0x87),  /* 72: CadetBlue */
    QERGB(0x5f, 0xaf, 0xaf),  /* 73: CadetBlue */
    QERGB(0x5f, 0xaf, 0xd7),  /* 74: SkyBlue3 */
    QERGB(0x5f, 0xaf, 0xff),  /* 75: SteelBlue1 */
    QERGB(0x5f, 0xd7, 0x00),  /* 76: Chartreuse3 */
    QERGB(0x5f, 0xd7, 0x5f),  /* 77: PaleGreen3 */
    QERGB(0x5f, 0xd7, 0x87),  /* 78: SeaGreen3 */
    QERGB(0x5f, 0xd7, 0xaf),  /* 79: Aquamarine3 */
    QERGB(0x5f, 0xd7, 0xd7),  /* 80: MediumTurquoise */
    QERGB(0x5f, 0xd7, 0xff),  /* 81: SteelBlue1 */
    QERGB(0x5f, 0xff, 0x00),  /* 82: Chartreuse2 */
    QERGB(0x5f, 0xff, 0x5f),  /* 83: SeaGreen2 */
    QERGB(0x5f, 0xff, 0x87),  /* 84: SeaGreen1 */
    QERGB(0x5f, 0xff, 0xaf),  /* 85: SeaGreen1 */
    QERGB(0x5f, 0xff, 0xd7),  /* 86: Aquamarine1 */
    QERGB(0x5f, 0xff, 0xff),  /* 87: DarkSlateGray2 */
    QERGB(0x87, 0x00, 0x00),  /* 88: DarkRed */
    QERGB(0x87, 0x00, 0x5f),  /* 89: DeepPink4 */
    QERGB(0x87, 0x00, 0x87),  /* 90: DarkMagenta */
    QERGB(0x87, 0x00, 0xaf),  /* 91: DarkMagenta */
    QERGB(0x87, 0x00, 0xd7),  /* 92: DarkViolet */
    QERGB(0x87, 0x00, 0xff),  /* 93: Purple */
    QERGB(0x87, 0x5f, 0x00),  /* 94: Orange4 */
    QERGB(0x87, 0x5f, 0x5f),  /* 95: LightPink4 */
    QERGB(0x87, 0x5f, 0x87),  /* 96: Plum4 */
    QERGB(0x87, 0x5f, 0xaf),  /* 97: MediumPurple3 */
    QERGB(0x87, 0x5f, 0xd7),  /* 98: MediumPurple3 */
    QERGB(0x87, 0x5f, 0xff),  /* 99: SlateBlue1 */
    QERGB(0x87, 0x87, 0x00),  /* 100: Yellow4 */
    QERGB(0x87, 0x87, 0x5f),  /* 101: Wheat4 */
    QERGB(0x87, 0x87, 0x87),  /* 102: Grey53 */
    QERGB(0x87, 0x87, 0xaf),  /* 103: LightSlateGrey */
    QERGB(0x87, 0x87, 0xd7),  /* 104: MediumPurple */
    QERGB(0x87, 0x87, 0xff),  /* 105: LightSlateBlue */
    QERGB(0x87, 0xaf, 0x00),  /* 106: Yellow4 */
    QERGB(0x87, 0xaf, 0x5f),  /* 107: DarkOliveGreen3 */
    QERGB(0x87, 0xaf, 0x87),  /* 108: DarkSeaGreen */
    QERGB(0x87, 0xaf, 0xaf),  /* 109: LightSkyBlue3 */
    QERGB(0x87, 0xaf, 0xd7),  /* 110: LightSkyBlue3 */
    QERGB(0x87, 0xaf, 0xff),  /* 111: SkyBlue2 */
    QERGB(0x87, 0xd7, 0x00),  /* 112: Chartreuse2 */
    QERGB(0x87, 0xd7, 0x5f),  /* 113: DarkOliveGreen3 */
    QERGB(0x87, 0xd7, 0x87),  /* 114: PaleGreen3 */
    QERGB(0x87, 0xd7, 0xaf),  /* 115: DarkSeaGreen3 */
    QERGB(0x87, 0xd7, 0xd7),  /* 116: DarkSlateGray3 */
    QERGB(0x87, 0xd7, 0xff),  /* 117: SkyBlue1 */
    QERGB(0x87, 0xff, 0x00),  /* 118: Chartreuse1 */
    QERGB(0x87, 0xff, 0x5f),  /* 119: LightGreen */
    QERGB(0x87, 0xff, 0x87),  /* 120: LightGreen */
    QERGB(0x87, 0xff, 0xaf),  /* 121: PaleGreen1 */
    QERGB(0x87, 0xff, 0xd7),  /* 122: Aquamarine1 */
    QERGB(0x87, 0xff, 0xff),  /* 123: DarkSlateGray1 */
    QERGB(0xaf, 0x00, 0x00),  /* 124: Red3 */
    QERGB(0xaf, 0x00, 0x5f),  /* 125: DeepPink4 */
    QERGB(0xaf, 0x00, 0x87),  /* 126: MediumVioletRed */
    QERGB(0xaf, 0x00, 0xaf),  /* 127: Magenta3 */
    QERGB(0xaf, 0x00, 0xd7),  /* 128: DarkViolet */
    QERGB(0xaf, 0x00, 0xff),  /* 129: Purple */
    QERGB(0xaf, 0x5f, 0x00),  /* 130: DarkOrange3 */
    QERGB(0xaf, 0x5f, 0x5f),  /* 131: IndianRed */
    QERGB(0xaf, 0x5f, 0x87),  /* 132: HotPink3 */
    QERGB(0xaf, 0x5f, 0xaf),  /* 133: MediumOrchid3 */
    QERGB(0xaf, 0x5f, 0xd7),  /* 134: MediumOrchid */
    QERGB(0xaf, 0x5f, 0xff),  /* 135: MediumPurple2 */
    QERGB(0xaf, 0x87, 0x00),  /* 136: DarkGoldenrod */
    QERGB(0xaf, 0x87, 0x5f),  /* 137: LightSalmon3 */
    QERGB(0xaf, 0x87, 0x87),  /* 138: RosyBrown */
    QERGB(0xaf, 0x87, 0xaf),  /* 139: Grey63 */
    QERGB(0xaf, 0x87, 0xd7),  /* 140: MediumPurple2 */
    QERGB(0xaf, 0x87, 0xff),  /* 141: MediumPurple1 */
    QERGB(0xaf, 0xaf, 0x00),  /* 142: Gold3 */
    QERGB(0xaf, 0xaf, 0x5f),  /* 143: DarkKhaki */
    QERGB(0xaf, 0xaf, 0x87),  /* 144: NavajoWhite3 */
    QERGB(0xaf, 0xaf, 0xaf),  /* 145: Grey69 */
    QERGB(0xaf, 0xaf, 0xd7),  /* 146: LightSteelBlue3 */
    QERGB(0xaf, 0xaf, 0xff),  /* 147: LightSteelBlue */
    QERGB(0xaf, 0xd7, 0x00),  /* 148: Yellow3 */
    QERGB(0xaf, 0xd7, 0x5f),  /* 149: DarkOliveGreen3 */
    QERGB(0xaf, 0xd7, 0x87),  /* 150: DarkSeaGreen3 */
    QERGB(0xaf, 0xd7, 0xaf),  /* 151: DarkSeaGreen2 */
    QERGB(0xaf, 0xd7, 0xd7),  /* 152: LightCyan3 */
    QERGB(0xaf, 0xd7, 0xff),  /* 153: LightSkyBlue1 */
    QERGB(0xaf, 0xff, 0x00),  /* 154: GreenYellow */
    QERGB(0xaf, 0xff, 0x5f),  /* 155: DarkOliveGreen2 */
    QERGB(0xaf, 0xff, 0x87),  /* 156: PaleGreen1 */
    QERGB(0xaf, 0xff, 0xaf),  /* 157: DarkSeaGreen2 */
    QERGB(0xaf, 0xff, 0xd7),  /* 158: DarkSeaGreen1 */
    QERGB(0xaf, 0xff, 0xff),  /* 159: PaleTurquoise1 */
    QERGB(0xd7, 0x00, 0x00),  /* 160: Red3 */
    QERGB(0xd7, 0x00, 0x5f),  /* 161: DeepPink3 */
    QERGB(0xd7, 0x00, 0x87),  /* 162: DeepPink3 */
    QERGB(0xd7, 0x00, 0xaf),  /* 163: Magenta3 */
    QERGB(0xd7, 0x00, 0xd7),  /* 164: Magenta3 */
    QERGB(0xd7, 0x00, 0xff),  /* 165: Magenta2 */
    QERGB(0xd7, 0x5f, 0x00),  /* 166: DarkOrange3 */
    QERGB(0xd7, 0x5f, 0x5f),  /* 167: IndianRed */
    QERGB(0xd7, 0x5f, 0x87),  /* 168: HotPink3 */
    QERGB(0xd7, 0x5f, 0xaf),  /* 169: HotPink2 */
    QERGB(0xd7, 0x5f, 0xd7),  /* 170: Orchid */
    QERGB(0xd7, 0x5f, 0xff),  /* 171: MediumOrchid1 */
    QERGB(0xd7, 0x87, 0x00),  /* 172: Orange3 */
    QERGB(0xd7, 0x87, 0x5f),  /* 173: LightSalmon3 */
    QERGB(0xd7, 0x87, 0x87),  /* 174: LightPink3 */
    QERGB(0xd7, 0x87, 0xaf),  /* 175: Pink3 */
    QERGB(0xd7, 0x87, 0xd7),  /* 176: Plum3 */
    QERGB(0xd7, 0x87, 0xff),  /* 177: Violet */
    QERGB(0xd7, 0xaf, 0x00),  /* 178: Gold3 */
    QERGB(0xd7, 0xaf, 0x5f),  /* 179: LightGoldenrod3 */
    QERGB(0xd7, 0xaf, 0x87),  /* 180: Tan */
    QERGB(0xd7, 0xaf, 0xaf),  /* 181: MistyRose3 */
    QERGB(0xd7, 0xaf, 0xd7),  /* 182: Thistle3 */
    QERGB(0xd7, 0xaf, 0xff),  /* 183: Plum2 */
    QERGB(0xd7, 0xd7, 0x00),  /* 184: Yellow3 */
    QERGB(0xd7, 0xd7, 0x5f),  /* 185: Khaki3 */
    QERGB(0xd7, 0xd7, 0x87),  /* 186: LightGoldenrod2 */
    QERGB(0xd7, 0xd7, 0xaf),  /* 187: LightYellow3 */
    QERGB(0xd7, 0xd7, 0xd7),  /* 188: Grey84 */
    QERGB(0xd7, 0xd7, 0xff),  /* 189: LightSteelBlue1 */
    QERGB(0xd7, 0xff, 0x00),  /* 190: Yellow2 */
    QERGB(0xd7, 0xff, 0x5f),  /* 191: DarkOliveGreen1 */
    QERGB(0xd7, 0xff, 0x87),  /* 192: DarkOliveGreen1 */
    QERGB(0xd7, 0xff, 0xaf),  /* 193: DarkSeaGreen1 */
    QERGB(0xd7, 0xff, 0xd7),  /* 194: Honeydew2 */
    QERGB(0xd7, 0xff, 0xff),  /* 195: LightCyan1 */
    QERGB(0xff, 0x00, 0x00),  /* 196: Red1 */
    QERGB(0xff, 0x00, 0x5f),  /* 197: DeepPink2 */
    QERGB(0xff, 0x00, 0x87),  /* 198: DeepPink1 */
    QERGB(0xff, 0x00, 0xaf),  /* 199: DeepPink1 */
    QERGB(0xff, 0x00, 0xd7),  /* 200: Magenta2 */
    QERGB(0xff, 0x00, 0xff),  /* 201: Magenta1 */
    QERGB(0xff, 0x5f, 0x00),  /* 202: OrangeRed1 */
    QERGB(0xff, 0x5f, 0x5f),  /* 203: IndianRed1 */
    QERGB(0xff, 0x5f, 0x87),  /* 204: IndianRed1 */
    QERGB(0xff, 0x5f, 0xaf),  /* 205: HotPink */
    QERGB(0xff, 0x5f, 0xd7),  /* 206: HotPink */
    QERGB(0xff, 0x5f, 0xff),  /* 207: MediumOrchid1 */
    QERGB(0xff, 0x87, 0x00),  /* 208: DarkOrange */
    QERGB(0xff, 0x87, 0x5f),  /* 209: Salmon1 */
    QERGB(0xff, 0x87, 0x87),  /* 210: LightCoral */
    QERGB(0xff, 0x87, 0xaf),  /* 211: PaleVioletRed1 */
    QERGB(0xff, 0x87, 0xd7),  /* 212: Orchid2 */
    QERGB(0xff, 0x87, 0xff),  /* 213: Orchid1 */
    QERGB(0xff, 0xaf, 0x00),  /* 214: Orange1 */
    QERGB(0xff, 0xaf, 0x5f),  /* 215: SandyBrown */
    QERGB(0xff, 0xaf, 0x87),  /* 216: LightSalmon1 */
    QERGB(0xff, 0xaf, 0xaf),  /* 217: LightPink1 */
    QERGB(0xff, 0xaf, 0xd7),  /* 218: Pink1 */
    QERGB(0xff, 0xaf, 0xff),  /* 219: Plum1 */
    QERGB(0xff, 0xd7, 0x00),  /* 220: Gold1 */
    QERGB(0xff, 0xd7, 0x5f),  /* 221: LightGoldenrod2 */
    QERGB(0xff, 0xd7, 0x87),  /* 222: LightGoldenrod2 */
    QERGB(0xff, 0xd7, 0xaf),  /* 223: NavajoWhite1 */
    QERGB(0xff, 0xd7, 0xd7),  /* 224: MistyRose1 */
    QERGB(0xff, 0xd7, 0xff),  /* 225: Thistle1 */
    QERGB(0xff, 0xff, 0x00),  /* 226: Yellow1 */
    QERGB(0xff, 0xff, 0x5f),  /* 227: LightGoldenrod1 */
    QERGB(0xff, 0xff, 0x87),  /* 228: Khaki1 */
    QERGB(0xff, 0xff, 0xaf),  /* 229: Wheat1 */
    QERGB(0xff, 0xff, 0xd7),  /* 230: Cornsilk1 */
    QERGB(0xff, 0xff, 0xff),  /* 231: Grey100 */
    QERGB(0x08, 0x08, 0x08),  /* 232: Grey3 */
    QERGB(0x12, 0x12, 0x12),  /* 233: Grey7 */
    QERGB(0x1c, 0x1c, 0x1c),  /* 234: Grey11 */
    QERGB(0x26, 0x26, 0x26),  /* 235: Grey15 */
    QERGB(0x30, 0x30, 0x30),  /* 236: Grey19 */
    QERGB(0x3a, 0x3a, 0x3a),  /* 237: Grey23 */
    QERGB(0x44, 0x44, 0x44),  /* 238: Grey27 */
    QERGB(0x4e, 0x4e, 0x4e),  /* 239: Grey30 */
    QERGB(0x58, 0x58, 0x58),  /* 240: Grey35 */
    QERGB(0x62, 0x62, 0x62),  /* 241: Grey39 */
    QERGB(0x6c, 0x6c, 0x6c),  /* 242: Grey42 */
    QERGB(0x76, 0x76, 0x76),  /* 243: Grey46 */
    QERGB(0x80, 0x80, 0x80),  /* 244: Grey50 */
    QERGB(0x8a, 0x8a, 0x8a),  /* 245: Grey54 */
    QERGB(0x94, 0x94, 0x94),  /* 246: Grey58 */
    QERGB(0x9e, 0x9e, 0x9e),  /* 247: Grey62 */
    QERGB(0xa8, 0xa8, 0xa8),  /* 248: Grey66 */
    QERGB(0xb2, 0xb2, 0xb2),  /* 249: Grey70 */
    QERGB(0xbc, 0xbc, 0xbc),  /* 250: Grey74 */
    QERGB(0xc6, 0xc6, 0xc6),  /* 251: Grey78 */
    QERGB(0xd0, 0xd0, 0xd0),  /* 252: Grey82 */
    QERGB(0xda, 0xda, 0xda),  /* 253: Grey85 */
    QERGB(0xe4, 0xe4, 0xe4),  /* 254: Grey89 */
    QERGB(0xee, 0xee, 0xee),  /* 255: Grey93 */
#endif
};

#if 0
static unsigned char const scale_cube[256] = {
    /* This array is used for mapping rgb colors to the standard palette */
    /* 216 entry RGB cube with axes 0,95,135,175,215,255 */
#define REP5(x)   (x), (x), (x), (x), (x)
#define REP10(x)  REP5(x), REP5(x)
#define REP20(x)  REP10(x), REP10(x)
#define REP40(x)  REP20(x), REP20(x)
#define REP25(x)  REP5(x), REP5(x), REP5(x), REP5(x), REP5(x)
#define REP47(x)  REP10(x), REP10(x), REP10(x), REP10(x), REP5(x), (x), (x)
    REP47(0), REP47(1), REP20(1), REP40(2), REP40(3), REP40(4), REP20(5), 5
};

static unsigned char const scale_grey[256] = {
    /* This array is used for mapping gray levels to the standard palette */
    /* 232..255: 24 entry grey scale 8,18..238 */
    16, 16, 16, 16,
    REP10(232), REP10(233), REP10(234), REP10(235),
    REP10(236), REP10(237), REP10(238), REP10(239),
    REP10(240), REP10(241), REP10(242), REP10(243),
    REP10(244), REP10(245), REP10(246), REP10(247),
    REP10(248), REP10(249), REP10(250), REP10(251),
    REP10(252), REP10(253), REP10(254), REP10(255),
    255, 255, 255,
    231, 231, 231, 231, 231, 231, 231, 231, 231
};
#endif

static inline int color_dist(QEColor c1, QEColor c2) {
    /* using casts because c1 and c2 are unsigned */
#if 0
    /* using a quick approximation to give green extra weight */
    return      abs((int)((c1 >>  0) & 0xff) - (int)((c2 >>  0) & 0xff)) +
            2 * abs((int)((c1 >>  8) & 0xff) - (int)((c2 >>  8) & 0xff)) +
                abs((int)((c1 >> 16) & 0xff) - (int)((c2 >> 16) & 0xff));
#else
    /* using different weights to R, G, B according to luminance levels */
    return  11 * abs((int)((c1 >>  0) & 0xff) - (int)((c2 >>  0) & 0xff)) +
            59 * abs((int)((c1 >>  8) & 0xff) - (int)((c2 >>  8) & 0xff)) +
            30 * abs((int)((c1 >> 16) & 0xff) - (int)((c2 >> 16) & 0xff));
#endif
}

/* XXX: should have a more generic API with precomputed mapping scales */
/* Convert RGB triplet to a composite color */
unsigned int qe_map_color(QEColor color, QEColor const *colors, int count, int *dist)
{
    int i, cmin, dmin, d;

    color &= 0xFFFFFF;  /* mask off the alpha channel */

    if (count >= 0x1000000) {
        cmin = color | 0x1000000;  /* force explicit RGB triplet */
        dmin = 0;
    } else {
        dmin = INT_MAX;
        cmin = 0;
        if (count <= 16) {
            for (i = 0; i < count; i++) {
                d = color_dist(color, colors[i]);
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                }
            }
        } else { /* if (dmin > 0 && count > 16) */
            unsigned int r = (color >> 16) & 0xff;
            unsigned int g = (color >>  8) & 0xff;
            unsigned int b = (color >>  0) & 0xff;
#if 0
            if (r == g && g == b) {
                i = scale_grey[r];
                d = color_dist(color, colors[i]);
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                }
            } else {
                /* XXX: should use more precise projection */
                i = 16 + scale_cube[r] * 36 +
                    scale_cube[g] * 6 +
                    scale_cube[b];
                d = color_dist(color, colors[i]);
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                }
            }
#else
            if (r == g && g == b) {
                /* color is a gray tone:
                 * map to the closest palette entry
                 */
                d = color_dist(color, colors[16]);
                if (d < dmin) {
                    cmin = 16;
                    dmin = d;
                }
                for (i = 231; i < 256; i++) {
                    d = color_dist(color, colors[i]);
                    if (d < dmin) {
                        cmin = i;
                        dmin = d;
                    }
                }
            } else {
                /* general case: try and match a palette entry
                 * from the 6x6x6 color cube .
                 */
                /* XXX: this causes gliches on true color terminals
                 * with a non standard xterm palette, such as iTerm2.
                 * On true color terminals, we should treat palette
                 * colors and rgb colors differently in the shell buffer
                 * terminal emulator.
                 */
                for (i = 16; i < 232; i++) {
                    d = color_dist(color, colors[i]);
                    if (d < dmin) {
                        cmin = i;
                        dmin = d;
                    }
                }
            }
#endif
            if (dmin > 0 && count >= 4096) {
                /* 13-bit 7936 color system */
                d = 0;
                for (;;) {
                    if (r == g) {
                        if (g == b) {  /* #xxxxxx */
                            i = 0x700 + r;
                            break;
                        }
                        if (r == 0) {  /* #0000xx */
                            i = 0x100 + b;
                            break;
                        }
                        if (r == 255) {  /* #FFFFxx */
                            i = 0x800 + 0x100 + b;
                            break;
                        }
                        if (b == 0) {  /* #xxxx00 */
                            i = 0x600 + r;
                            break;
                        }
                        if (b == 255) {  /* #xxxxFF */
                            i = 0x800 + 0x600 + r;
                            break;
                        }
                    } else
                    if (r == b) {
                        if (r == 0) {  /* #00xx00 */
                            i = 0x200 + g;
                            break;
                        }
                        if (r == 255) {  /* #FFxxFF */
                            i = 0x800 + 0x200 + g;
                            break;
                        }
                        if (g == 0) {  /* #xx00xx */
                            i = 0x500 + r;
                            break;
                        }
                        if (g == 255) {  /* #xxFFxx */
                            i = 0x800 + 0x500 + r;
                            break;
                        }
                    } else
                    if (g == b) {
                        if (g == 0) {  /* #xx0000 */
                            i = 0x400 + r;
                            break;
                        }
                        if (g == 255) {  /* #xxFFFF */
                            i = 0x800 + 0x400 + r;
                            break;
                        }
                        if (r == 0) {  /* #00xxxx */
                            i = 0x300 + g;
                            break;
                        }
                        if (r == 255) {  /* #FFxxxx */
                            i = 0x800 + 0x300 + g;
                            break;
                        }
                    }
                    i = 0x1000 | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4);
                    d = color_dist(color, (color & 0xF0F0F0) | ((color & 0xF0F0F0) >> 4));
                    break;
                }
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                }
            }
        }
    }
    if (dist) {
        *dist = dmin;
    }
    return cmin;
}

/* Convert a composite color to an RGB triplet */
QEColor qe_unmap_color(int color, int count) {
    /* XXX: Should use an 8K array for all colors <= 8192 */
    if (color < 256) {
        return xterm_colors[color];
    }
    if (color < 8192) {
        /* 13-bit 7936 color system */
        if (color & 0x1000) {
            /* explicit 12-bit color */
            QEColor rgb = (((color & 0xF00) << 12) |
                           ((color & 0x0F0) <<  4) |
                           ((color & 0x00F) <<  0));
            return rgb | (rgb << 4);
        }
        if ((color & 0xf00) < 0xf00) {
            /* 256 level color ramps */
            /* 0x800 is unused and converts to white */
            int r, g, b;
            r = g = b = color & 0xFF;
            if (!(color & 0x400)) r = -(color >> 11) & 0xFF;
            if (!(color & 0x200)) g = -(color >> 11) & 0xFF;
            if (!(color & 0x100)) b = -(color >> 11) & 0xFF;
            return QERGB(r, g, b);
        } else {
            /* 0xf00 indicates the standard xterm color palette */
            return xterm_colors[color & 255];
        }
    }
    /* explicit RGB color */
    return color & 0xFFFFFF;
}

static int css_lookup_color(ColorDef const *def, int count,
                            const char *name)
{
    int i;

    for (i = 0; i < count; i++) {
        if (!strxcmp(def[i].name, name))
            return i;
    }
    return -1;
}

int css_define_color(const char *name, const char *value)
{
    ColorDef *def;
    QEColor color;
    int index;

    /* Check color validity */
    if (css_get_color(&color, value))
        return -1;

    /* First color definition: allocate modifiable array */
    if (qe_colors == default_colors) {
        qe_colors = qe_malloc_dup(default_colors, sizeof(default_colors));
    }

    /* Make room: reallocate table in chunks of 8 entries */
    if (((nb_qe_colors - nb_default_colors) & 7) == 0) {
        if (!qe_realloc(&qe_colors,
                        (nb_qe_colors + 8) * sizeof(ColorDef))) {
            return -1;
        }
    }
    /* Check for redefinition */
    index = css_lookup_color(qe_colors, nb_qe_colors, name);
    if (index >= 0) {
        qe_colors[index].color = color;
        return 0;
    }

    def = &qe_colors[nb_qe_colors];
    def->name = qe_strdup(name);
    def->color = color;
    nb_qe_colors++;

    return 0;
}

void css_free_colors(void)
{
    if (qe_colors != default_colors) {
        while (nb_qe_colors > nb_default_colors) {
            nb_qe_colors--;
            qe_free(unconst(char **)&qe_colors[nb_qe_colors].name);
        }
        qe_free(&qe_colors);
        qe_colors = unconst(ColorDef *)default_colors;
    }
}

/* XXX: make HTML parsing optional ? */
int css_get_color(QEColor *color_ptr, const char *p)
{
    ColorDef const *def;
    int count, index, len, v, i, n;
    unsigned char rgba[4];

    /* search in tables */
    def = qe_colors;
    count = nb_qe_colors;
    index = css_lookup_color(def, count, p);
    if (index >= 0) {
        *color_ptr = def[index].color;
        return 0;
    }

    rgba[3] = 0xff;
    if (qe_isxdigit((unsigned char)*p)) {
        goto parse_num;
    } else
    if (*p == '#') {
        /* handle '#' notation */
        p++;
    parse_num:
        len = strlen(p);
        switch (len) {
        case 3:
            for (i = 0; i < 3; i++) {
                v = qe_digit_value(*p++);
                rgba[i] = v | (v << 4);
            }
            break;
        case 6:
            for (i = 0; i < 3; i++) {
                v = qe_digit_value(*p++) << 4;
                v |= qe_digit_value(*p++);
                rgba[i] = v;
            }
            break;
        default:
            /* error */
            return -1;
        }
    } else
    if (strstart(p, "rgb(", &p)) {
        n = 3;
        goto parse_rgba;
    } else
    if (strstart(p, "rgba(", &p)) {
        /* extension for alpha */
        n = 4;
    parse_rgba:
        for (i = 0; i < n; i++) {
            /* XXX: floats ? */
            qe_skip_spaces(&p);
            v = strtol_c(p, &p, 0);
            if (*p == '%') {
                v = (v * 255) / 100;
                p++;
            }
            rgba[i] = v;
            if (qe_skip_spaces(&p) == ',')
                p++;
        }
    } else {
        return -1;
    }
    *color_ptr = (rgba[0] << 16) | (rgba[1] << 8) |
        (rgba[2]) | (rgba[3] << 24);
    return 0;
}

/* return 0 if unknown font */
int css_get_font_family(const char *str)
{
    int v;

    if (!strcasecmp(str, "serif") ||
        !strcasecmp(str, "times"))
        v = QE_FONT_FAMILY_SERIF;
    else
    if (!strcasecmp(str, "sans") ||
        !strcasecmp(str, "arial") ||
        !strcasecmp(str, "helvetica"))
        v = QE_FONT_FAMILY_SANS;
    else
    if (!strcasecmp(str, "fixed") ||
        !strcasecmp(str, "monospace") ||
        !strcasecmp(str, "courier"))
        v = QE_FONT_FAMILY_FIXED;
    else
        v = 0; /* inherit */
    return v;
}
#endif  /* style stuff */

/* scans a comma separated list of entries, return index of match or -1 */
/* CG: very similar to strfind */
int css_get_enum(const char *str, const char *enum_str) {
    int val, len;
    const char *s, *s0;

    len = strlen(str);
    s = enum_str;
    val = 0;
    for (;;) {
        for (s0 = s; *s && *s != ','; s++)
            continue;
        if ((s - s0) == len && !memcmp(s0, str, len))
            return val;
        if (!*s)
            break;
        s++;
        val++;
    }
    return -1;
}

/* a = a union b */
void css_union_rect(CSSRect *a, const CSSRect *b)
{
    if (css_is_null_rect(b))
        return;
    if (css_is_null_rect(a)) {
        *a = *b;
    } else {
        if (b->x1 < a->x1)
            a->x1 = b->x1;
        if (b->y1 < a->y1)
            a->y1 = b->y1;
        if (b->x2 > a->x2)
            a->x2 = b->x2;
        if (b->y2 > a->y2)
            a->y2 = b->y2;
    }
}
