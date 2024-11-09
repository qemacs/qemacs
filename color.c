/*
 * Colors for qemacs.
 *
 * Copyright (c) 2001 Fabrice Bellard.
 * Copyright (c) 2002-2024 Charlie Gordon.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <limits.h>

#include "util.h"
#include "color.h"

#if 1
/* For 8K colors, We use a color system with 7936 colors:
 *   - 16 standard colors
 *   - 240 standard palette colors
 *   - 4096 colors in a 16x16x16 cube
 *   - a 256 level grey ramp
 *   - 6 256-level fade to black ramps
 *   - 6 256-level fade to white ramps
 *   - a 512 palette of custom colors for named colors not found hereabove
 */

/* Alternately we could use a system with 8157 colors:
 *   - 2 default color
 *   - 16 standard colors
 *   - 256 standard palette colors
 *   - 6859 colors in a 19x19x19 cube
 *     with ramp 0,15,31,47,63,79,95,108,121,135,
 *               148,161,175,188,201,215,228,241,255 values
 *   - 256 level grey ramp
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

#define rgb(r,g,b)  QERGB(r,g,b)

static ColorDef const default_colors[] = {
#if 0
    /* From HTML 4.0 spec */
    { "aqua",                 rgb(0,255,255)   }, // #00FFFF
    { "black",                rgb(0,0,0)       }, // #000000
    { "blue",                 rgb(0,0,255)     }, // #0000FF
    { "cyan",                 rgb(0,255,255)   }, // #00FFFF aqua
    { "fuchsia",              rgb(255,0,255)   }, // #FF00FF
    { "gray",                 rgb(190,190,190) }, // #BEBEBE
    { "green",                rgb(0,128,0)     }, // #008000
    { "grey",                 rgb(190,190,190) }, // #BEBEBE grey
    { "lime",                 rgb(0,255,0)     }, // #00FF00
    { "magenta",              rgb(255,0,255)   }, // #FF00FF fuchsia
    { "maroon",               rgb(128,0,0)     }, // #800000
    { "navy",                 rgb(0,0,128)     }, // #000080
    { "olive",                rgb(128,128,0)   }, // #808000
    { "purple",               rgb(128,0,128)   }, // #800080
    { "red",                  rgb(255,0,0)     }, // #FF0000
    { "silver",               rgb(192,192,192) }, // #C0C0C0
    { "teal",                 rgb(0,128,128)   }, // #008080
    { "white",                rgb(255,255,255) }, // #FFFFFF
    { "yellow",               rgb(255,255,0)   }, // #FFFF00
    { "transparent",          COLOR_TRANSPARENT },
#else
    /* CSS Basic color names */
    { "aqua",                 rgb(0,255,255)   }, // #00FFFF  hsl(180,100%,50%)
    { "black",                rgb(0,0,0)       }, // #000000  hsl(0,0%,0%)
    { "blue",                 rgb(0,0,255)     }, // #0000FF  hsl(240,100%,50%)
    { "cyan",                 rgb(0,255,255)   }, // #00FFFF  hsl(180,100%,50%)
    { "fuchsia",              rgb(255,0,255)   }, // #FF00FF  hsl(300,100%,50%)
    { "gray",                 rgb(190,190,190) }, // #BEBEBE  (hack: not the css value)
    //{ "gray",                 rgb(128,128,128) }, // * #808080  hsl(0,0%,50%) (changed)
    { "green",                rgb(0,128,0)     }, // * #008000  hsl(120,100%,25%)
    { "grey",                 rgb(190,190,190) }, // #BEBEBE gray
    //{ "grey",                 rgb(128,128,128) }, // * #808080  hsl(0,0%,50%) (changed)
    { "lime",                 rgb(0,255,0)     }, // #00FF00  hsl(120,100%,50%)
    { "magenta",              rgb(255,0,255)   }, // #FF00FF  hsl(300,100%,50%)
    { "maroon",               rgb(128,0,0)     }, // * #800000  hsl(0,100%,25%)
    { "navy",                 rgb(0,0,128)     }, // #000080  hsl(240,100%,25%)
    { "olive",                rgb(128,128,0)   }, // #808000  hsl(60,100%,25%)
    { "purple",               rgb(128,0,128)   }, // * #800080  hsl(300,100%,25%)
    { "red",                  rgb(255,0,0)     }, // #FF0000  hsl(0,100%,50%)
    { "silver",               rgb(192,192,192) }, // #C0C0C0  hsl(0,0%,75%)
    { "teal",                 rgb(0,128,128)   }, // #008080  hsl(180,100%,25%)
    { "white",                rgb(255,255,255) }, // #FFFFFF  hsl(0,0%,100%)
    { "yellow",               rgb(255,255,0)   }, // #FFFF00  hsl(60,100%,50%)
    { "transparent",          COLOR_TRANSPARENT },

    /* CSS Extended color names */
    { "alice-blue",           rgb(240,248,255) }, // #F0F8FF  hsl(208,100%,97.06%)
    { "antique-white",        rgb(250,235,215) }, // #FAEBD7  hsl(34.29,77.78%,91.18%)
    { "aquamarine",           rgb(127,255,212) }, // #7FFFD4  hsl(159.84,100%,74.9%)
    { "azure",                rgb(240,255,255) }, // #F0FFFF  hsl(180,100%,97.06%)
    { "beige",                rgb(245,245,220) }, // #F5F5DC  hsl(60,55.56%,91.18%)
    { "bisque",               rgb(255,228,196) }, // #FFE4C4  hsl(32.54,100%,88.43%)
    { "blanched-almond",      rgb(255,235,205) }, // #FFEBCD  hsl(36,100%,90.2%)
    { "blue-violet",          rgb(138,43,226)  }, // #8A2BE2  hsl(271.15,75.93%,52.75%)
    { "brown",                rgb(165,42,42)   }, // #A52A2A  hsl(0,59.42%,40.59%)
    { "burlywood",            rgb(222,184,135) }, // #DED887  hsl(33.79,56.86%,70%)
    { "cadet-blue",           rgb(95,158,160)  }, // #5F9EA0  hsl(181.85,25.49%,50%)
    { "chartreuse",           rgb(127,255,0)   }, // #7FFF00  hsl(90.12,100%,50%)
    { "chocolate",            rgb(210,105,30)  }, // #D2691E  hsl(25,75%,47.06%)
    { "coral",                rgb(255,127,80)  }, // #FF7F50  hsl(16.11,100%,65.69%)
    { "cornflower-blue",      rgb(100,149,237) }, // #6495ED  hsl(218.54,79.19%,66.08%)
    { "cornsilk",             rgb(255,248,220) }, // #FFF8DC  hsl(48,100%,93.14%)
    { "crimson",              rgb(220,20,60)   }, // #DC143C  hsl(348,83.33%,47.06%)
    { "dark-blue",            rgb(0,0,139)     }, // blue4 #00008B  hsl(240,100%,27.25%)
    { "dark-cyan",            rgb(0,139,139)   }, // cyan4 #008B8B  hsl(180,100%,27.25%)
    { "dark-goldenrod",       rgb(184,134,11)  }, // #B8860B  hsl(42.66,88.72%,38.24%)
    { "dark-gray",            rgb(169,169,169) }, // #A9A9A9  hsl(0,0%,66.27%)
    { "dark-green",           rgb(0,100,0)     }, // #006400  hsl(120,100%,19.61%)
    { "dark-khaki",           rgb(189,183,107) }, // #BDB76B  hsl(55.61,38.32%,58.04%)
    { "dark-magenta",         rgb(139,0,139)   }, // magenta4 #8B008B  hsl(300,100%,27.25%)
    { "dark-olive-green",     rgb(85,107,47)   }, // #556B2F  hsl(82,38.96%,30.2%)
    { "dark-orange",          rgb(255,140,0)   }, // #FF8C00  hsl(32.94,100%,50%)
    { "dark-orchid",          rgb(153,50,204)  }, // #9932CC  hsl(280.13,60.63%,49.8%)
    { "dark-red",             rgb(139,0,0)     }, // red4 #8B0000  hsl(0,100%,27.25%)
    { "dark-salmon",          rgb(233,150,122) }, // #E9967A  hsl(15.14,71.61%,69.61%)
    { "dark-sea-green",       rgb(143,188,143) }, // #8FBC8F  hsl(120,25.14%,64.9%)
    { "dark-slate-blue",      rgb(72,61,139)   }, // #483D8B  hsl(248.46,39%,39.22%)
    { "dark-slate-gray",      rgb(47,79,79)    }, // #2F4F4F  hsl(180,25.4%,24.71%)
    { "dark-turquoise",       rgb(0,206,209)   }, // #00CED1  hsl(180.86,100%,40.98%)
    { "dark-violet",          rgb(148,0,211)   }, // #9400D3  hsl(282.09,100%,41.37%)
    { "deep-pink",            rgb(255,20,147)  }, // #FF1493  hsl(327.57,100%,53.92%)
    { "deep-sky-blue",        rgb(0,191,255)   }, // #00BFFF  hsl(195.06,100%,50%)
    { "dim-gray",             rgb(105,105,105) }, // #696969  hsl(0,0%,41.18%)
    { "dodger-blue",          rgb(30,144,255)  }, // #1E90FF  hsl(209.6,100%,55.88%)
    { "firebrick",            rgb(178,34,34)   }, // #B22222  hsl(0,67.92%,41.57%)
    { "floral-white",         rgb(255,250,240) }, // #FFFAF0  hsl(40,100%,97.06%)
    { "forest-green",         rgb(34,139,34)   }, // #228B22  hsl(120,60.69%,33.92%)
    { "gainsboro",            rgb(220,220,220) }, // #DCDCDC  hsl(0,0%,86.27%)
    { "ghost-white",          rgb(248,248,255) }, // #F8F8FF  hsl(0,0%,0%)
    { "gold",                 rgb(255,215,0)   }, // #FFD700  hsl(50.59,100%,50%)
    { "goldenrod",            rgb(218,165,32)  }, // #DAA520  hsl(42.9,74.4%,49.02%)
    { "green-yellow",         rgb(173,255,47)  }, // #ADFF2F  hsl(83.65,100%,59.22%)
    { "honeydew",             rgb(240,255,240) }, // #F0FFF0  hsl(120,100%,97.06%)
    { "hot-pink",             rgb(255,105,180) }, // #FF69B4  hsl(330,100%,70.59%)
    { "indian-red",           rgb(205,92,92)   }, // #CD5C5C  hsl(0,53.05%,58.24%)
    { "indigo",               rgb(75,0,130)    }, // #4B0082  hsl(274.62,100%,25.49%)
    { "ivory",                rgb(255,255,240) }, // #FFFFF0  hsl(60,100%,97.06%)
    { "khaki",                rgb(240,230,140) }, // #F0E68C  hsl(54,76.92%,74.51%)
    { "lavender",             rgb(230,230,250) }, // #E6E6FA  hsl(240,66.67%,94.12%)
    { "lavender-blush",       rgb(255,240,245) }, // #FFF0F5  hsl(340,100%,97.06%)
    { "lawn-green",           rgb(124,252,0)   }, // #7CFC00  hsl(90.48,100%,49.41%)
    { "lemon-chiffon",        rgb(255,250,205) }, // #FFFACD  hsl(54,100%,90.2%)
    { "light-blue",           rgb(173,216,230) }, // #ADD8E6  hsl(194.74,53.27%,79.02%)
    { "light-coral",          rgb(240,128,128) }, // #F08080  hsl(0,78.87%,72.16%)
    { "light-cyan",           rgb(224,255,255) }, // #E0FFFF  hsl(180,100%,93.92%)
    { "light-goldenrod",      rgb(238,221,130) }, // #E4DD82
    { "light-goldenrod-yellow", rgb(250,250,210) }, // #FAFAD2  hsl(60,80%,90.2%)
    { "light-gray",           rgb(211,211,211) }, // #D3D3D3  hsl(0,0%,82.75%)
    { "light-green",          rgb(144,238,144) }, // #90EE90  hsl(120,73.44%,74.9%)
    { "light-pink",           rgb(255,182,193) }, // #FFB6C1  hsl(350.96,100%,85.69%)
    { "light-salmon",         rgb(255,160,122) }, // #FFA07A  hsl(17.14,100%,73.92%)
    { "light-sea-green",      rgb(32,178,170)  }, // #20B2AA  hsl(176.71,69.52%,41.18%)
    { "light-sky-blue",       rgb(135,206,250) }, // #87CEFA  hsl(202.96,92%,75.49%)
    { "light-slate-gray",     rgb(119,136,153) }, // #778899  hsl(210,14.29%,53.33%)
    { "light-steel-blue",     rgb(176,196,222) }, // #B0C4DE  hsl(213.91,41.07%,78.04%)
    { "light-yellow",         rgb(255,255,224) }, // #FFFFE0  hsl(60,100%,93.92%)
    { "lime",                 rgb(0,255,0)     }, // #00FF00  hsl(120,100%,50%)
    { "lime-green",           rgb(50,205,50)   }, // #32CD32  hsl(120,60.78%,50%)
    { "linen",                rgb(250,240,230) }, // #FAF0E6  hsl(30,66.67%,94.12%)
    { "medium-aquamarine",    rgb(102,205,170) }, // #66CDAA  hsl(159.61,50.74%,60.2%)
    { "medium-blue",          rgb(0,0,205)     }, // #0000CD  hsl(240,100%,40.2%)
    { "medium-orchid",        rgb(186,85,211)  }, // #BA55D3  hsl(288.1,58.88%,58.04%)
    { "medium-purple",        rgb(147,112,219) }, // #9370DB  hsl(259.63,59.78%,64.9%)
    { "medium-sea-green",     rgb(60,179,113)  }, // #3CB371  hsl(146.72,49.79%,46.86%)
    { "medium-slate-blue",    rgb(123,104,238) }, // #7B68EE  hsl(248.51,79.76%,67.06%)
    { "medium-spring-green",  rgb(0,250,154)   }, // #00FA9A  hsl(156.96,100%,49.02%)
    { "medium-turquoise",     rgb(72,209,204)  }, // #48D1CC  hsl(177.81,59.83%,55.1%)
    { "medium-violet-red",    rgb(199,21,133)  }, // #C71585  hsl(322.25,80.91%,43.14%)
    { "midnight-blue",        rgb(25,25,112)   }, // #191970  hsl(240,63.5%,26.86%)
    { "mint-cream",           rgb(245,255,250) }, // #F5FFFA  hsl(150,100%,98.04%)
    { "misty-rose",           rgb(255,228,225) }, // #FFE4E1  hsl(6,100%,94.12%)
    { "moccasin",             rgb(255,228,181) }, // #FFE4B5  hsl(38.11,100%,85.49%)
    { "navajo-white",         rgb(255,222,173) }, // #FFDEAD  hsl(35.85,100%,83.92%)
    { "old-lace",             rgb(253,245,230) }, // #FDF5E6  hsl(39.13,85.19%,94.71%)
    { "olive-drab",           rgb(107,142,35)  }, // #6B8E23  hsl(79.63,60.45%,34.71%)
    { "orange",               rgb(255,165,0)   }, // #FFA500  hsl(38.82,100%,50%)
    { "orange-red",           rgb(255,69,0)    }, // #FF4500  hsl(16.24,100%,50%)
    { "orchid",               rgb(218,112,214) }, // #DA70D6  hsl(302.26,58.89%,64.71%)
    { "pale-goldenrod",       rgb(238,232,170) }, // #EEE8AA  hsl(54.71,66.67%,80%)
    { "pale-green",           rgb(152,251,152) }, // #98FB98  hsl(120,92.52%,79.02%)
    { "pale-turquoise",       rgb(175,238,238) }, // #AFEEEE  hsl(180,64.95%,80.98%)
    { "pale-violet-red",      rgb(219,112,147) }, // #DB7093  hsl(340.37,59.78%,64.9%)
    { "papaya-whip",          rgb(255,239,213) }, // #FFEFD5  hsl(37.14,100%,91.76%)
    { "peach-puff",           rgb(255,218,185) }, // #FFDAB9  hsl(28.29,100%,86.27%)
    { "peru",                 rgb(205,133,63)  }, // #CD853F  hsl(29.58,58.68%,52.55%)
    { "pink",                 rgb(255,192,203) }, // #FFC0CB  hsl(349.52,100%,87.65%)
    { "plum",                 rgb(221,160,221) }, // #DDA0DD  hsl(300,47.29%,74.71%)
    { "powder-blue",          rgb(176,224,230) }, // #B0E0E6  hsl(186.67,51.92%,79.61%)
    { "rebecca-purple",       rgb(102,51,153)  }, // #663399  hsl(270,50%,40%)
    { "rosy-brown",           rgb(188,143,143) }, // #BC8F8F  hsl(0,25.14%,64.9%)
    { "royal-blue",           rgb(65,105,225)  }, // #4169E1  hsl(225,72.73%,56.86%)
    { "saddle-brown",         rgb(139,69,19)   }, // #8B4513  hsl(25,75.95%,30.98%)
    { "salmon",               rgb(250,128,114) }, // #FA8072  hsl(6.18,93.15%,71.37%)
    { "sandy-brown",          rgb(244,164,96)  }, // #F4A460  hsl(27.57,87.06%,66.67%)
    { "sea-green",            rgb(46,139,87)   }, // #2E8B57  hsl(146.45,50.27%,36.27%)
    { "seashell",             rgb(255,245,238) }, // #FFF5EE  hsl(24.71,100%,96.67%)
    { "sienna",               rgb(160,82,45)   }, // #A0522D  hsl(19.3,56.1%,40.2%)
    { "sky-blue",             rgb(135,206,235) }, // #87CEEB  hsl(197.4,71.43%,72.55%)
    { "slate-blue",           rgb(106,90,205)  }, // #6A5ACD  hsl(248.35,53.49%,57.84%)
    { "slate-gray",           rgb(112,128,144) }, // #708090  hsl(210,12.6%,50.2%)
    { "snow",                 rgb(255,250,250) }, // #FFFAFA  hsl(0,100%,99.02%)
    { "spring-green",         rgb(0,255,127)   }, // #00FF7F  hsl(149.88,100%,50%)
    { "steel-blue",           rgb(70,130,180)  }, // #4682B4  hsl(207.27,44%,49.02%)
    { "tan",                  rgb(210,180,140) }, // #D2B48C  hsl(34.29,43.75%,68.63%)
    { "thistle",              rgb(216,191,216) }, // #D8BFD8  hsl(300,24.27%,79.8%)
    { "tomato",               rgb(255,99,71)   }, // #FF6347  hsl(9.13,100%,63.92%)
    { "turquoise",            rgb(64,224,208)  }, // #40E0D0  hsl(174,72.07%,56.47%)
    { "violet",               rgb(238,130,238) }, // #EE82EE  hsl(300,76.06%,72.16%)
    { "wheat",                rgb(245,222,179) }, // #F5DEB3  hsl(39.09,76.74%,83.14%)
    { "white-smoke",          rgb(245,245,245) }, // #F5F5F5  hsl(0,0%,96.08%)
    { "yellow-green",         rgb(154,205,50)  }, // #9ACD32  hsl(79.74,60.78%,50%)
    // X11 extended color names
    { "light-slate-blue",     rgb(132,112,255) },
    { "navy-blue",            rgb(0,0,128)     },
    { "web-gray",             rgb(128,128,128) },
    { "web-green",            rgb(0,128,0)     },
    { "web-maroon",           rgb(128,0,0)     },
    { "web-purple",           rgb(128,0,128)   },
    { "x11-gray",             rgb(190,190,190) },
    { "x11-green",            rgb(0,255,0)     },
    { "x11-maroon",           rgb(176,48,96)   },
    { "x11-purple",           rgb(160,32,240)  },
#ifndef CONFIG_TINY
    // X11 numbered variants
    { "antique-white1",       rgb(255,239,219) },
    { "antique-white2",       rgb(238,223,204) },
    { "antique-white3",       rgb(205,192,176) },
    { "antique-white4",       rgb(139,131,120) },
    { "aquamarine1",          rgb(127,255,212) },
    { "aquamarine2",          rgb(118,238,198) },
    { "aquamarine3",          rgb(102,205,170) },
    { "aquamarine4",          rgb(69,139,116)  },
    { "azure1",               rgb(240,255,255) },
    { "azure2",               rgb(224,238,238) },
    { "azure3",               rgb(193,205,205) },
    { "azure4",               rgb(131,139,139) },
    { "bisque1",              rgb(255,228,196) },
    { "bisque2",              rgb(238,213,183) },
    { "bisque3",              rgb(205,183,158) },
    { "bisque4",              rgb(139,125,107) },
    { "blue1",                rgb(0,0,255)     },
    { "blue2",                rgb(0,0,238)     },
    { "blue3",                rgb(0,0,205)     },
    { "blue4",                rgb(0,0,139)     },
    { "brown1",               rgb(255,64,64)   },
    { "brown2",               rgb(238,59,59)   },
    { "brown3",               rgb(205,51,51)   },
    { "brown4",               rgb(139,35,35)   },
    { "burlywood1",           rgb(255,211,155) },
    { "burlywood2",           rgb(238,197,145) },
    { "burlywood3",           rgb(205,170,125) },
    { "burlywood4",           rgb(139,115,85)  },
    { "cadet-blue1",          rgb(152,245,255) },
    { "cadet-blue2",          rgb(142,229,238) },
    { "cadet-blue3",          rgb(122,197,205) },
    { "cadet-blue4",          rgb(83,134,139)  },
    { "chartreuse1",          rgb(127,255,0)   },
    { "chartreuse2",          rgb(118,238,0)   },
    { "chartreuse3",          rgb(102,205,0)   },
    { "chartreuse4",          rgb(69,139,0)    },
    { "chocolate1",           rgb(255,127,36)  },
    { "chocolate2",           rgb(238,118,33)  },
    { "chocolate3",           rgb(205,102,29)  },
    { "chocolate4",           rgb(139,69,19)   },
    { "coral1",               rgb(255,114,86)  },
    { "coral2",               rgb(238,106,80)  },
    { "coral3",               rgb(205,91,69)   },
    { "coral4",               rgb(139,62,47)   },
    { "cornsilk1",            rgb(255,248,220) },
    { "cornsilk2",            rgb(238,232,205) },
    { "cornsilk3",            rgb(205,200,177) },
    { "cornsilk4",            rgb(139,136,120) },
    { "cyan1",                rgb(0,255,255)   },
    { "cyan2",                rgb(0,238,238)   },
    { "cyan3",                rgb(0,205,205)   },
    { "cyan4",                rgb(0,139,139)   },
    { "dark-goldenrod1",      rgb(255,185,15)  },
    { "dark-goldenrod2",      rgb(238,173,14)  },
    { "dark-goldenrod3",      rgb(205,149,12)  },
    { "dark-goldenrod4",      rgb(139,101,8)   },
    { "dark-olive-green1",    rgb(202,255,112) },
    { "dark-olive-green2",    rgb(188,238,104) },
    { "dark-olive-green3",    rgb(162,205,90)  },
    { "dark-olive-green4",    rgb(110,139,61)  },
    { "dark-orange1",         rgb(255,127,0)   },
    { "dark-orange2",         rgb(238,118,0)   },
    { "dark-orange3",         rgb(205,102,0)   },
    { "dark-orange4",         rgb(139,69,0)    },
    { "dark-orchid1",         rgb(191,62,255)  },
    { "dark-orchid2",         rgb(178,58,238)  },
    { "dark-orchid3",         rgb(154,50,205)  },
    { "dark-orchid4",         rgb(104,34,139)  },
    { "dark-sea-green1",      rgb(193,255,193) },
    { "dark-sea-green2",      rgb(180,238,180) },
    { "dark-sea-green3",      rgb(155,205,155) },
    { "dark-sea-green4",      rgb(105,139,105) },
    { "dark-slate-gray1",     rgb(151,255,255) },
    { "dark-slate-gray2",     rgb(141,238,238) },
    { "dark-slate-gray3",     rgb(121,205,205) },
    { "dark-slate-gray4",     rgb(82,139,139)  },
    { "deep-pink1",           rgb(255,20,147)  },
    { "deep-pink2",           rgb(238,18,137)  },
    { "deep-pink3",           rgb(205,16,118)  },
    { "deep-pink4",           rgb(139,10,80)   },
    { "deep-sky-blue1",       rgb(0,191,255)   },
    { "deep-sky-blue2",       rgb(0,178,238)   },
    { "deep-sky-blue3",       rgb(0,154,205)   },
    { "deep-sky-blue4",       rgb(0,104,139)   },
    { "dodger-blue1",         rgb(30,144,255)  },
    { "dodger-blue2",         rgb(28,134,238)  },
    { "dodger-blue3",         rgb(24,116,205)  },
    { "dodger-blue4",         rgb(16,78,139)   },
    { "firebrick1",           rgb(255,48,48)   },
    { "firebrick2",           rgb(238,44,44)   },
    { "firebrick3",           rgb(205,38,38)   },
    { "firebrick4",           rgb(139,26,26)   },
    { "gold1",                rgb(255,215,0)   },
    { "gold2",                rgb(238,201,0)   },
    { "gold3",                rgb(205,173,0)   },
    { "gold4",                rgb(139,117,0)   },
    { "goldenrod1",           rgb(255,193,37)  },
    { "goldenrod2",           rgb(238,180,34)  },
    { "goldenrod3",           rgb(205,155,29)  },
    { "goldenrod4",           rgb(139,105,20)  },
    { "green1",               rgb(0,255,0)     },
    { "green2",               rgb(0,238,0)     },
    { "green3",               rgb(0,205,0)     },
    { "green4",               rgb(0,139,0)     },
    { "honeydew1",            rgb(240,255,240) },
    { "honeydew2",            rgb(224,238,224) },
    { "honeydew3",            rgb(193,205,193) },
    { "honeydew4",            rgb(131,139,131) },
    { "hot-pink1",            rgb(255,110,180) },
    { "hot-pink2",            rgb(238,106,167) },
    { "hot-pink3",            rgb(205,96,144)  },
    { "hot-pink4",            rgb(139,58,98)   },
    { "indian-red1",          rgb(255,106,106) },
    { "indian-red2",          rgb(238,99,99)   },
    { "indian-red3",          rgb(205,85,85)   },
    { "indian-red4",          rgb(139,58,58)   },
    { "ivory1",               rgb(255,255,240) },
    { "ivory2",               rgb(238,238,224) },
    { "ivory3",               rgb(205,205,193) },
    { "ivory4",               rgb(139,139,131) },
    { "khaki1",               rgb(255,246,143) },
    { "khaki2",               rgb(238,230,133) },
    { "khaki3",               rgb(205,198,115) },
    { "khaki4",               rgb(139,134,78)  },
    { "lavender-blush1",      rgb(255,240,245) },
    { "lavender-blush2",      rgb(238,224,229) },
    { "lavender-blush3",      rgb(205,193,197) },
    { "lavender-blush4",      rgb(139,131,134) },
    { "lemon-chiffon1",       rgb(255,250,205) },
    { "lemon-chiffon2",       rgb(238,233,191) },
    { "lemon-chiffon3",       rgb(205,201,165) },
    { "lemon-chiffon4",       rgb(139,137,112) },
    { "light-blue1",          rgb(191,239,255) },
    { "light-blue2",          rgb(178,223,238) },
    { "light-blue3",          rgb(154,192,205) },
    { "light-blue4",          rgb(104,131,139) },
    { "light-cyan1",          rgb(224,255,255) },
    { "light-cyan2",          rgb(209,238,238) },
    { "light-cyan3",          rgb(180,205,205) },
    { "light-cyan4",          rgb(122,139,139) },
    { "light-goldenrod1",     rgb(255,236,139) },
    { "light-goldenrod2",     rgb(238,220,130) },
    { "light-goldenrod3",     rgb(205,190,112) },
    { "light-goldenrod4",     rgb(139,129,76)  },
    { "light-pink1",          rgb(255,174,185) },
    { "light-pink2",          rgb(238,162,173) },
    { "light-pink3",          rgb(205,140,149) },
    { "light-pink4",          rgb(139,95,101)  },
    { "light-salmon1",        rgb(255,160,122) },
    { "light-salmon2",        rgb(238,149,114) },
    { "light-salmon3",        rgb(205,129,98)  },
    { "light-salmon4",        rgb(139,87,66)   },
    { "light-sky-blue1",      rgb(176,226,255) },
    { "light-sky-blue2",      rgb(164,211,238) },
    { "light-sky-blue3",      rgb(141,182,205) },
    { "light-sky-blue4",      rgb(96,123,139)  },
    { "light-steel-blue1",    rgb(202,225,255) },
    { "light-steel-blue2",    rgb(188,210,238) },
    { "light-steel-blue3",    rgb(162,181,205) },
    { "light-steel-blue4",    rgb(110,123,139) },
    { "light-yellow1",        rgb(255,255,224) },
    { "light-yellow2",        rgb(238,238,209) },
    { "light-yellow3",        rgb(205,205,180) },
    { "light-yellow4",        rgb(139,139,122) },
    { "magenta1",             rgb(255,0,255)   },
    { "magenta2",             rgb(238,0,238)   },
    { "magenta3",             rgb(205,0,205)   },
    { "magenta4",             rgb(139,0,139)   },
    { "maroon1",              rgb(255,52,179)  },
    { "maroon2",              rgb(238,48,167)  },
    { "maroon3",              rgb(205,41,144)  },
    { "maroon4",              rgb(139,28,98)   },
    { "medium-orchid1",       rgb(224,102,255) },
    { "medium-orchid2",       rgb(209,95,238)  },
    { "medium-orchid3",       rgb(180,82,205)  },
    { "medium-orchid4",       rgb(122,55,139)  },
    { "medium-purple1",       rgb(171,130,255) },
    { "medium-purple2",       rgb(159,121,238) },
    { "medium-purple3",       rgb(137,104,205) },
    { "medium-purple4",       rgb(93,71,139)   },
    { "misty-rose1",          rgb(255,228,225) },
    { "misty-rose2",          rgb(238,213,210) },
    { "misty-rose3",          rgb(205,183,181) },
    { "misty-rose4",          rgb(139,125,123) },
    { "navajo-white1",        rgb(255,222,173) },
    { "navajo-white2",        rgb(238,207,161) },
    { "navajo-white3",        rgb(205,179,139) },
    { "navajo-white4",        rgb(139,121,94)  },
    { "olive-drab1",          rgb(192,255,62)  },
    { "olive-drab2",          rgb(179,238,58)  },
    { "olive-drab3",          rgb(154,205,50)  },
    { "olive-drab4",          rgb(105,139,34)  },
    { "orange-red1",          rgb(255,69,0)    },
    { "orange-red2",          rgb(238,64,0)    },
    { "orange-red3",          rgb(205,55,0)    },
    { "orange-red4",          rgb(139,37,0)    },
    { "orange1",              rgb(255,165,0)   },
    { "orange2",              rgb(238,154,0)   },
    { "orange3",              rgb(205,133,0)   },
    { "orange4",              rgb(139,90,0)    },
    { "orchid1",              rgb(255,131,250) },
    { "orchid2",              rgb(238,122,233) },
    { "orchid3",              rgb(205,105,201) },
    { "orchid4",              rgb(139,71,137)  },
    { "pale-green1",          rgb(154,255,154) },
    { "pale-green2",          rgb(144,238,144) },
    { "pale-green3",          rgb(124,205,124) },
    { "pale-green4",          rgb(84,139,84)   },
    { "pale-turquoise1",      rgb(187,255,255) },
    { "pale-turquoise2",      rgb(174,238,238) },
    { "pale-turquoise3",      rgb(150,205,205) },
    { "pale-turquoise4",      rgb(102,139,139) },
    { "pale-violet-red1",     rgb(255,130,171) },
    { "pale-violet-red2",     rgb(238,121,159) },
    { "pale-violet-red3",     rgb(205,104,137) },
    { "pale-violet-red4",     rgb(139,71,93)   },
    { "peach-puff1",          rgb(255,218,185) },
    { "peach-puff2",          rgb(238,203,173) },
    { "peach-puff3",          rgb(205,175,149) },
    { "peach-puff4",          rgb(139,119,101) },
    { "pink1",                rgb(255,181,197) },
    { "pink2",                rgb(238,169,184) },
    { "pink3",                rgb(205,145,158) },
    { "pink4",                rgb(139,99,108)  },
    { "plum1",                rgb(255,187,255) },
    { "plum2",                rgb(238,174,238) },
    { "plum3",                rgb(205,150,205) },
    { "plum4",                rgb(139,102,139) },
    { "purple1",              rgb(155,48,255)  },
    { "purple2",              rgb(145,44,238)  },
    { "purple3",              rgb(125,38,205)  },
    { "purple4",              rgb(85,26,139)   },
    { "red1",                 rgb(255,0,0)     },
    { "red2",                 rgb(238,0,0)     },
    { "red3",                 rgb(205,0,0)     },
    { "red4",                 rgb(139,0,0)     },
    { "rosy-brown1",          rgb(255,193,193) },
    { "rosy-brown2",          rgb(238,180,180) },
    { "rosy-brown3",          rgb(205,155,155) },
    { "rosy-brown4",          rgb(139,105,105) },
    { "royal-blue1",          rgb(72,118,255)  },
    { "royal-blue2",          rgb(67,110,238)  },
    { "royal-blue3",          rgb(58,95,205)   },
    { "royal-blue4",          rgb(39,64,139)   },
    { "salmon1",              rgb(255,140,105) },
    { "salmon2",              rgb(238,130,98)  },
    { "salmon3",              rgb(205,112,84)  },
    { "salmon4",              rgb(139,76,57)   },
    { "sea-green1",           rgb(84,255,159)  },
    { "sea-green2",           rgb(78,238,148)  },
    { "sea-green3",           rgb(67,205,128)  },
    { "sea-green4",           rgb(46,139,87)   },
    { "seashell1",            rgb(255,245,238) },
    { "seashell2",            rgb(238,229,222) },
    { "seashell3",            rgb(205,197,191) },
    { "seashell4",            rgb(139,134,130) },
    { "sienna1",              rgb(255,130,71)  },
    { "sienna2",              rgb(238,121,66)  },
    { "sienna3",              rgb(205,104,57)  },
    { "sienna4",              rgb(139,71,38)   },
    { "sky-blue1",            rgb(135,206,255) },
    { "sky-blue2",            rgb(126,192,238) },
    { "sky-blue3",            rgb(108,166,205) },
    { "sky-blue4",            rgb(74,112,139)  },
    { "slate-blue1",          rgb(131,111,255) },
    { "slate-blue2",          rgb(122,103,238) },
    { "slate-blue3",          rgb(105,89,205)  },
    { "slate-blue4",          rgb(71,60,139)   },
    { "slate-gray1",          rgb(198,226,255) },
    { "slate-gray2",          rgb(185,211,238) },
    { "slate-gray3",          rgb(159,182,205) },
    { "slate-gray4",          rgb(108,123,139) },
    { "snow1",                rgb(255,250,250) },
    { "snow2",                rgb(238,233,233) },
    { "snow3",                rgb(205,201,201) },
    { "snow4",                rgb(139,137,137) },
    { "spring-green1",        rgb(0,255,127)   },
    { "spring-green2",        rgb(0,238,118)   },
    { "spring-green3",        rgb(0,205,102)   },
    { "spring-green4",        rgb(0,139,69)    },
    { "steel-blue1",          rgb(99,184,255)  },
    { "steel-blue2",          rgb(92,172,238)  },
    { "steel-blue3",          rgb(79,148,205)  },
    { "steel-blue4",          rgb(54,100,139)  },
    { "tan1",                 rgb(255,165,79)  },
    { "tan2",                 rgb(238,154,73)  },
    { "tan3",                 rgb(205,133,63)  },
    { "tan4",                 rgb(139,90,43)   },
    { "thistle1",             rgb(255,225,255) },
    { "thistle2",             rgb(238,210,238) },
    { "thistle3",             rgb(205,181,205) },
    { "thistle4",             rgb(139,123,139) },
    { "tomato1",              rgb(255,99,71)   },
    { "tomato2",              rgb(238,92,66)   },
    { "tomato3",              rgb(205,79,57)   },
    { "tomato4",              rgb(139,54,38)   },
    { "turquoise1",           rgb(0,245,255)   },
    { "turquoise2",           rgb(0,229,238)   },
    { "turquoise3",           rgb(0,197,205)   },
    { "turquoise4",           rgb(0,134,139)   },
    { "violet-red1",          rgb(255,62,150)  },
    { "violet-red2",          rgb(238,58,140)  },
    { "violet-red3",          rgb(205,50,120)  },
    { "violet-red4",          rgb(139,34,82)   },
    { "wheat1",               rgb(255,231,186) },
    { "wheat2",               rgb(238,216,174) },
    { "wheat3",               rgb(205,186,150) },
    { "wheat4",               rgb(139,126,102) },
    { "yellow1",              rgb(255,255,0)   },
    { "yellow2",              rgb(238,238,0)   },
    { "yellow3",              rgb(205,205,0)   },
    { "yellow4",              rgb(139,139,0)   },
#endif
#ifndef CONFIG_TINY
    // X11 gray scale
    { "gray0",                rgb(0,0,0)       }, // 0 0 0 0 0
    { "gray1",                rgb(3,3,3)       }, // 3 3 3 3 3
    { "gray2",                rgb(5,5,5)       }, // 5 5 5 5 5
    { "gray3",                rgb(8,8,8)       }, // 8 8 8 8 8
    { "gray4",                rgb(10,10,10)    }, // 10 10 10 10 10
    { "gray5",                rgb(13,13,13)    }, // 13 13 13 13 13
    { "gray6",                rgb(15,15,15)    }, // 15 15 15 15 15
    { "gray7",                rgb(18,18,18)    }, // 18 18 18 18 18
    { "gray8",                rgb(20,20,20)    }, // 20 20 20 20 20
    { "gray9",                rgb(23,23,23)    }, // 23 23 23 23 23
    { "gray10",               rgb(26,26,26)    }, // 26 25< 25 25 25
    { "gray11",               rgb(28,28,28)    }, // 28 28 28 28 28
    { "gray12",               rgb(31,31,31)    }, // 31 31 31 31 31
    { "gray13",               rgb(33,33,33)    }, // 33 33 33 33 33
    { "gray14",               rgb(36,36,36)    }, // 36 36 36 36 36
    { "gray15",               rgb(38,38,38)    }, // 38 38 38 38 38
    { "gray16",               rgb(41,41,41)    }, // 41 41 41 41 41
    { "gray17",               rgb(43,43,43)    }, // 43 43 43 43 43
    { "gray18",               rgb(46,46,46)    }, // 46 46 46 46 46
    { "gray19",               rgb(48,48,48)    }, // 48 48 48 48 48
    { "gray20",               rgb(51,51,51)    }, // 51 51 51 51 51
    { "gray21",               rgb(54,54,54)    }, // 54 54 54 54 54
    { "gray22",               rgb(56,56,56)    }, // 56 56 56 56 56
    { "gray23",               rgb(59,59,59)    }, // 59 59 59 59 59
    { "gray24",               rgb(61,61,61)    }, // 61 61 61 61 61
    { "gray25",               rgb(64,64,64)    }, // 64 64 64 64 64
    { "gray26",               rgb(66,66,66)    }, // 66 66 66 66 66
    { "gray27",               rgb(69,69,69)    }, // 69 69 69 69 69
    { "gray28",               rgb(71,71,71)    }, // 71 71 71 71 71
    { "gray29",               rgb(74,74,74)    }, // 74 74 74 74 74
    { "gray30",               rgb(77,77,77)    }, // 77 76< 76 76 76
    { "gray31",               rgb(79,79,79)    }, // 79 79 79 79 79
    { "gray32",               rgb(82,82,82)    }, // 82 82 82 82 82
    { "gray33",               rgb(84,84,84)    }, // 84 84 84 84 84
    { "gray34",               rgb(87,87,87)    }, // 87 87 87 87 87
    { "gray35",               rgb(89,89,89)    }, // 89 89 89 89 89
    { "gray36",               rgb(92,92,92)    }, // 92 92 92 92 92
    { "gray37",               rgb(94,94,94)    }, // 94 94 94 94 94
    { "gray38",               rgb(97,97,97)    }, // 97 97 97 97 97
    { "gray39",               rgb(99,99,99)    }, // 99 99 99 99 99
    { "gray40",               rgb(102,102,102) }, // 102 102 102 102 102
    { "gray41",               rgb(105,105,105) }, // 105 105 105 105 105
    { "gray42",               rgb(107,107,107) }, // 107 107 107 107 107
    { "gray43",               rgb(110,110,110) }, // 110 110 110 110 110
    { "gray44",               rgb(112,112,112) }, // 112 112 112 112 112
    { "gray45",               rgb(115,115,115) }, // 115 115 115 115 115
    { "gray46",               rgb(117,117,117) }, // 117 117 117 117 117
    { "gray47",               rgb(120,120,120) }, // 120 120 120 120 120
    { "gray48",               rgb(122,122,122) }, // 122 122 122 122 122
    { "gray49",               rgb(125,125,125) }, // 125 125 125 125 125
    { "gray50",               rgb(127,127,127) }, // 128 127= 127 127 127
    { "gray51",               rgb(130,130,130) }, // 130 130 130 130 130
    { "gray52",               rgb(133,133,133) }, // 133 133 133 133 133
    { "gray53",               rgb(135,135,135) }, // 135 135 135 135 135
    { "gray54",               rgb(138,138,138) }, // 138 138 138 138 138
    { "gray55",               rgb(140,140,140) }, // 140 140 140 140 140
    { "gray56",               rgb(143,143,143) }, // 143 143 143 143 143
    { "gray57",               rgb(145,145,145) }, // 145 145 145 145 145
    { "gray58",               rgb(148,148,148) }, // 148 148 148 148 148
    { "gray59",               rgb(150,150,150) }, // 150 150 150 150 150
    { "gray60",               rgb(153,153,153) }, // 153 153 153 153 153
    { "gray61",               rgb(156,156,156) }, // 156 156 156 156 156
    { "gray62",               rgb(158,158,158) }, // 158 158 158 158 158
    { "gray63",               rgb(161,161,161) }, // 161 161 161 161 161
    { "gray64",               rgb(163,163,163) }, // 163 163 163 163 163
    { "gray65",               rgb(166,166,166) }, // 166 166 166 166 166
    { "gray66",               rgb(168,168,168) }, // 168 168 168 168 168
    { "gray67",               rgb(171,171,171) }, // 171 171 171 171 171
    { "gray68",               rgb(173,173,173) }, // 173 173 173 173 173
    { "gray69",               rgb(176,176,176) }, // 176 176 176 176 176
    { "gray70",               rgb(179,179,179) }, // 179 178< 178 178 178
    { "gray71",               rgb(181,181,181) }, // 181 181 181 181 181
    { "gray72",               rgb(184,184,184) }, // 184 184 184 184 184
    { "gray73",               rgb(186,186,186) }, // 186 186 186 186 186
    { "gray74",               rgb(189,189,189) }, // 189 189 189 189 189
    { "gray75",               rgb(191,191,191) }, // 191 191 191 191 191
    { "gray76",               rgb(194,194,194) }, // 194 194 194 194 194
    { "gray77",               rgb(196,196,196) }, // 196 196 196 196 196
    { "gray78",               rgb(199,199,199) }, // 199 199 199 199 199
    { "gray79",               rgb(201,201,201) }, // 201 201 201 201 201
    { "gray80",               rgb(204,204,204) }, // 204 204 204 204 204
    { "gray81",               rgb(207,207,207) }, // 207 207 207 207 207
    { "gray82",               rgb(209,209,209) }, // 209 209 209 209 209
    { "gray83",               rgb(212,212,212) }, // 212 212 212 212 212
    { "gray84",               rgb(214,214,214) }, // 214 214 214 214 214
    { "gray85",               rgb(217,217,217) }, // 217 217 217 217 217
    { "gray86",               rgb(219,219,219) }, // 219 219 219 219 219
    { "gray87",               rgb(222,222,222) }, // 222 222 222 222 222
    { "gray88",               rgb(224,224,224) }, // 224 224 224 224 224
    { "gray89",               rgb(227,227,227) }, // 227 227 227 227 227
    { "gray90",               rgb(229,229,229) }, // 230 229= 229 229 229
    { "gray91",               rgb(232,232,232) }, // 232 232 232 232 232
    { "gray92",               rgb(235,235,235) }, // 235 235 235 235 235
    { "gray93",               rgb(237,237,237) }, // 237 237 237 237 237
    { "gray94",               rgb(240,240,240) }, // 240 240 240 240 240
    { "gray95",               rgb(242,242,242) }, // 242 242 242 242 242
    { "gray96",               rgb(245,245,245) }, // 245 245 245 245 245
    { "gray97",               rgb(247,247,247) }, // 247 247 247 247 247
    { "gray98",               rgb(250,250,250) }, // 250 250 250 250 250
    { "gray99",               rgb(252,252,252) }, // 252 252 252 252 252
    { "gray100",              rgb(255,255,255) }, // 255 255 255 255 255
#endif
#endif
#if 1
    /* Missing emacs colors */
    { "bright-black",         rgb(127,127,127) }, // #7F7F7F
    { "bright-blue",          rgb(92,92,255)   }, // #5C5CFF
    { "bright-cyan",          rgb(0,255,255)   }, // #00FFFF
    { "bright-green",         rgb(0,255,0)     }, // #00FF00
    { "bright-magenta",       rgb(255,0,255)   }, // #FF00FF
    { "bright-red",           rgb(255,0,0)     }, // #FF0000
    { "bright-white",         rgb(255,255,255) }, // #FFFFFF
    { "bright-yellow",        rgb(255,255,0)   }, // #FFFF00
    { "violet-red",           rgb(208,32,144)  }, // #D02090
#endif
};
#define nb_default_colors  countof(default_colors)

static QEColor custom_colors[512];
static int nb_custom_colors;

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
#if 1
    // XXX: we cannot not use these palette entries in qe_map_color
    //      unless the actual values have been queried from the terminal
    /* original SYSTEM palette */
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
#else
    /* xterm SYSTEM palette (CGA/Windows colors) */
    rgb(0,0,0),       /*   0: #000000 hsl(0,0%,0%)      Black */
    rgb(128,0,0),     /*   1: #800000 hsl(0,100%,25%)   Maroon */
    rgb(0,128,0),     /*   2: #008000 hsl(120,100%,25%) Green */
    rgb(128,128,0),   /*   3: #808000 hsl(60,100%,25%)  Olive */
    rgb(0,0,128),     /*   4: #000080 hsl(240,100%,25%) Navy */
    rgb(128,0,128),   /*   5: #800080 hsl(300,100%,25%) Purple */
    rgb(0,128,128),   /*   6: #008080 hsl(180,100%,25%) Teal */
    rgb(192,192,192), /*   7: #c0c0c0 hsl(0,0%,75%)     Silver */
    rgb(128,128,128), /*   8: #808080 hsl(0,0%,50%)     Grey */
    rgb(255,0,0),     /*   9: #ff0000 hsl(0,100%,50%)   Red */
    rgb(0,255,0),     /*  10: #00ff00 hsl(120,100%,50%) Lime */
    rgb(255,255,0),   /*  11: #ffff00 hsl(60,100%,50%)  Yellow */
    rgb(0,0,255),     /*  12: #0000ff hsl(240,100%,50%) Blue */
    rgb(255,0,255),   /*  13: #ff00ff hsl(300,100%,50%) Fuchsia */
    rgb(0,255,255),   /*  14: #00ffff hsl(180,100%,50%) Aqua */
    rgb(255,255,255), /*  15: #ffffff hsl(0,0%,100%)    White */
#endif
#if 1
    /* Extended color palette for xterm 256 color mode */

    /* From XFree86: xc/programs/xterm/256colres.h,
     * v 1.5 2002/10/05 17:57:11 dickey Exp
     */

    /* 216 entry RGB cube with axes 0,95,135,175,215,255 */
    /* followed by 24 entry grey scale 8,18..238 */
    // XXX: the default iTerm2 color cube uses slightly different tones
    rgb(0,0,0),       /*  16: #000000 hsl(0,0%,0%)      Grey0 */
    rgb(0,0,95),      /*  17: #00005f hsl(240,100%,18%) NavyBlue */
    rgb(0,0,135),     /*  18: #000087 hsl(240,100%,26%) DarkBlue */
    rgb(0,0,175),     /*  19: #0000af hsl(240,100%,34%) Blue3 */
    rgb(0,0,215),     /*  20: #0000d7 hsl(240,100%,42%) Blue3 */
    rgb(0,0,255),     /*  21: #0000ff hsl(240,100%,50%) Blue1 */
    rgb(0,95,0),      /*  22: #005f00 hsl(120,100%,18%) DarkGreen */
    rgb(0,95,95),     /*  23: #005f5f hsl(180,100%,18%) DeepSkyBlue4 */
    rgb(0,95,135),    /*  24: #005f87 hsl(97,100%,26%)  DeepSkyBlue4 */
    rgb(0,95,175),    /*  25: #005faf hsl(07,100%,34%)  DeepSkyBlue4 */
    rgb(0,95,215),    /*  26: #005fd7 hsl(13,100%,42%)  DodgerBlue3 */
    rgb(0,95,255),    /*  27: #005fff hsl(17,100%,50%)  DodgerBlue2 */
    rgb(0,135,0),     /*  28: #008700 hsl(120,100%,26%) Green4 */
    rgb(0,135,95),    /*  29: #00875f hsl(62,100%,26%)  SpringGreen4 */
    rgb(0,135,135),   /*  30: #008787 hsl(180,100%,26%) Turquoise4 */
    rgb(0,135,175),   /*  31: #0087af hsl(93,100%,34%)  DeepSkyBlue3 */
    rgb(0,135,215),   /*  32: #0087d7 hsl(02,100%,42%)  DeepSkyBlue3 */
    rgb(0,135,255),   /*  33: #0087ff hsl(08,100%,50%)  DodgerBlue1 */
    rgb(0,175,0),     /*  34: #00af00 hsl(120,100%,34%) Green3 */
    rgb(0,175,95),    /*  35: #00af5f hsl(52,100%,34%)  SpringGreen3 */
    rgb(0,175,135),   /*  36: #00af87 hsl(66,100%,34%)  DarkCyan */
    rgb(0,175,175),   /*  37: #00afaf hsl(180,100%,34%) LightSeaGreen */
    rgb(0,175,215),   /*  38: #00afd7 hsl(91,100%,42%)  DeepSkyBlue2 */
    rgb(0,175,255),   /*  39: #00afff hsl(98,100%,50%)  DeepSkyBlue1 */
    rgb(0,215,0),     /*  40: #00d700 hsl(120,100%,42%) Green3 */
    rgb(0,215,95),    /*  41: #00d75f hsl(46,100%,42%)  SpringGreen3 */
    rgb(0,215,135),   /*  42: #00d787 hsl(57,100%,42%)  SpringGreen2 */
    rgb(0,215,175),   /*  43: #00d7af hsl(68,100%,42%)  Cyan3 */
    rgb(0,215,215),   /*  44: #00d7d7 hsl(180,100%,42%) DarkTurquoise */
    rgb(0,215,255),   /*  45: #00d7ff hsl(89,100%,50%)  Turquoise2 */
    rgb(0,255,0),     /*  46: #00ff00 hsl(120,100%,50%) Green1 */
    rgb(0,255,95),    /*  47: #00ff5f hsl(42,100%,50%)  SpringGreen2 */
    rgb(0,255,135),   /*  48: #00ff87 hsl(51,100%,50%)  SpringGreen1 */
    rgb(0,255,175),   /*  49: #00ffaf hsl(61,100%,50%)  MediumSpringGreen */
    rgb(0,255,215),   /*  50: #00ffd7 hsl(70,100%,50%)  Cyan2 */
    rgb(0,255,255),   /*  51: #00ffff hsl(180,100%,50%) Cyan1 */
    rgb(95,0,0),      /*  52: #5f0000 hsl(0,100%,18%)   DarkRed */
    rgb(95,0,95),     /*  53: #5f005f hsl(300,100%,18%) DeepPink4 */
    rgb(95,0,135),    /*  54: #5f0087 hsl(82,100%,26%)  Purple4 */
    rgb(95,0,175),    /*  55: #5f00af hsl(72,100%,34%)  Purple4 */
    rgb(95,0,215),    /*  56: #5f00d7 hsl(66,100%,42%)  Purple3 */
    rgb(95,0,255),    /*  57: #5f00ff hsl(62,100%,50%)  BlueViolet */
    rgb(95,95,0),     /*  58: #5f5f00 hsl(60,100%,18%)  Orange4 */
    rgb(95,95,95),    /*  59: #5f5f5f hsl(0,0%,37%)     Grey37 */
    rgb(95,95,135),   /*  60: #5f5f87 hsl(240,17%,45%)  MediumPurple4 */
    rgb(95,95,175),   /*  61: #5f5faf hsl(240,33%,52%)  SlateBlue3 */
    rgb(95,95,215),   /*  62: #5f5fd7 hsl(240,60%,60%)  SlateBlue3 */
    rgb(95,95,255),   /*  63: #5f5fff hsl(240,100%,68%) RoyalBlue1 */
    rgb(95,135,0),    /*  64: #5f8700 hsl(7,100%,26%)   Chartreuse4 */
    rgb(95,135,95),   /*  65: #5f875f hsl(120,17%,45%)  DarkSeaGreen4 */
    rgb(95,135,135),  /*  66: #5f8787 hsl(180,17%,45%)  PaleTurquoise4 */
    rgb(95,135,175),  /*  67: #5f87af hsl(210,33%,52%)  SteelBlue */
    rgb(95,135,215),  /*  68: #5f87d7 hsl(220,60%,60%)  SteelBlue3 */
    rgb(95,135,255),  /*  69: #5f87ff hsl(225,100%,68%) CornflowerBlue */
    rgb(95,175,0),    /*  70: #5faf00 hsl(7,100%,34%)   Chartreuse3 */
    rgb(95,175,95),   /*  71: #5faf5f hsl(120,33%,52%)  DarkSeaGreen4 */
    rgb(95,175,135),  /*  72: #5faf87 hsl(150,33%,52%)  CadetBlue */
    rgb(95,175,175),  /*  73: #5fafaf hsl(180,33%,52%)  CadetBlue */
    rgb(95,175,215),  /*  74: #5fafd7 hsl(200,60%,60%)  SkyBlue3 */
    rgb(95,175,255),  /*  75: #5fafff hsl(210,100%,68%) SteelBlue1 */
    rgb(95,215,0),    /*  76: #5fd700 hsl(3,100%,42%)   Chartreuse3 */
    rgb(95,215,95),   /*  77: #5fd75f hsl(120,60%,60%)  PaleGreen3 */
    rgb(95,215,135),  /*  78: #5fd787 hsl(140,60%,60%)  SeaGreen3 */
    rgb(95,215,175),  /*  79: #5fd7af hsl(160,60%,60%)  Aquamarine3 */
    rgb(95,215,215),  /*  80: #5fd7d7 hsl(180,60%,60%)  MediumTurquoise */
    rgb(95,215,255),  /*  81: #5fd7ff hsl(195,100%,68%) SteelBlue1 */
    rgb(95,255,0),    /*  82: #5fff00 hsl(7,100%,50%)   Chartreuse2 */
    rgb(95,255,95),   /*  83: #5fff5f hsl(120,100%,68%) SeaGreen2 */
    rgb(95,255,135),  /*  84: #5fff87 hsl(135,100%,68%) SeaGreen1 */
    rgb(95,255,175),  /*  85: #5fffaf hsl(150,100%,68%) SeaGreen1 */
    rgb(95,255,215),  /*  86: #5fffd7 hsl(165,100%,68%) Aquamarine1 */
    rgb(95,255,255),  /*  87: #5fffff hsl(180,100%,68%) DarkSlateGray2 */
    rgb(135,0,0),     /*  88: #870000 hsl(0,100%,26%)   DarkRed */
    rgb(135,0,95),    /*  89: #87005f hsl(17,100%,26%)  DeepPink4 */
    rgb(135,0,135),   /*  90: #870087 hsl(300,100%,26%) DarkMagenta */
    rgb(135,0,175),   /*  91: #8700af hsl(86,100%,34%)  DarkMagenta */
    rgb(135,0,215),   /*  92: #8700d7 hsl(77,100%,42%)  DarkViolet */
    rgb(135,0,255),   /*  93: #8700ff hsl(71,100%,50%)  Purple */
    rgb(135,95,0),    /*  94: #875f00 hsl(2,100%,26%)   Orange4 */
    rgb(135,95,95),   /*  95: #875f5f hsl(0,17%,45%)    LightPink4 */
    rgb(135,95,135),  /*  96: #875f87 hsl(300,17%,45%)  Plum4 */
    rgb(135,95,175),  /*  97: #875faf hsl(270,33%,52%)  MediumPurple3 */
    rgb(135,95,215),  /*  98: #875fd7 hsl(260,60%,60%)  MediumPurple3 */
    rgb(135,95,255),  /*  99: #875fff hsl(255,100%,68%) SlateBlue1 */
    rgb(135,135,0),   /* 100: #878700 hsl(60,100%,26%)  Yellow4 */
    rgb(135,135,95),  /* 101: #87875f hsl(60,17%,45%)   Wheat4 */
    rgb(135,135,135), /* 102: #878787 hsl(0,0%,52%)     Grey53 */
    rgb(135,135,175), /* 103: #8787af hsl(240,20%,60%)  LightSlateGrey */
    rgb(135,135,215), /* 104: #8787d7 hsl(240,50%,68%)  MediumPurple */
    rgb(135,135,255), /* 105: #8787ff hsl(240,100%,76%) LightSlateBlue */
    rgb(135,175,0),   /* 106: #87af00 hsl(3,100%,34%)   Yellow4 */
    rgb(135,175,95),  /* 107: #87af5f hsl(90,33%,52%)   DarkOliveGreen3 */
    rgb(135,175,135), /* 108: #87af87 hsl(120,20%,60%)  DarkSeaGreen */
    rgb(135,175,175), /* 109: #87afaf hsl(180,20%,60%)  LightSkyBlue3 */
    rgb(135,175,215), /* 110: #87afd7 hsl(210,50%,68%)  LightSkyBlue3 */
    rgb(135,175,255), /* 111: #87afff hsl(220,100%,76%) SkyBlue2 */
    rgb(135,215,0),   /* 112: #87d700 hsl(2,100%,42%)   Chartreuse2 */
    rgb(135,215,95),  /* 113: #87d75f hsl(100,60%,60%)  DarkOliveGreen3 */
    rgb(135,215,135), /* 114: #87d787 hsl(120,50%,68%)  PaleGreen3 */
    rgb(135,215,175), /* 115: #87d7af hsl(150,50%,68%)  DarkSeaGreen3 */
    rgb(135,215,215), /* 116: #87d7d7 hsl(180,50%,68%)  DarkSlateGray3 */
    rgb(135,215,255), /* 117: #87d7ff hsl(200,100%,76%) SkyBlue1 */
    rgb(135,255,0),   /* 118: #87ff00 hsl(8,100%,50%)   Chartreuse1 */
    rgb(135,255,95),  /* 119: #87ff5f hsl(105,100%,68%) LightGreen */
    rgb(135,255,135), /* 120: #87ff87 hsl(120,100%,76%) LightGreen */
    rgb(135,255,175), /* 121: #87ffaf hsl(140,100%,76%) PaleGreen1 */
    rgb(135,255,215), /* 122: #87ffd7 hsl(160,100%,76%) Aquamarine1 */
    rgb(135,255,255), /* 123: #87ffff hsl(180,100%,76%) DarkSlateGray1 */
    rgb(175,0,0),     /* 124: #af0000 hsl(0,100%,34%)   Red3 */
    rgb(175,0,95),    /* 125: #af005f hsl(27,100%,34%)  DeepPink4 */
    rgb(175,0,135),   /* 126: #af0087 hsl(13,100%,34%)  MediumVioletRed */
    rgb(175,0,175),   /* 127: #af00af hsl(300,100%,34%) Magenta3 */
    rgb(175,0,215),   /* 128: #af00d7 hsl(88,100%,42%)  DarkViolet */
    rgb(175,0,255),   /* 129: #af00ff hsl(81,100%,50%)  Purple */
    rgb(175,95,0),    /* 130: #af5f00 hsl(2,100%,34%)   DarkOrange3 */
    rgb(175,95,95),   /* 131: #af5f5f hsl(0,33%,52%)    IndianRed */
    rgb(175,95,135),  /* 132: #af5f87 hsl(330,33%,52%)  HotPink3 */
    rgb(175,95,175),  /* 133: #af5faf hsl(300,33%,52%)  MediumOrchid3 */
    rgb(175,95,215),  /* 134: #af5fd7 hsl(280,60%,60%)  MediumOrchid */
    rgb(175,95,255),  /* 135: #af5fff hsl(270,100%,68%) MediumPurple2 */
    rgb(175,135,0),   /* 136: #af8700 hsl(6,100%,34%)   DarkGoldenrod */
    rgb(175,135,95),  /* 137: #af875f hsl(30,33%,52%)   LightSalmon3 */
    rgb(175,135,135), /* 138: #af8787 hsl(0,20%,60%)    RosyBrown */
    rgb(175,135,175), /* 139: #af87af hsl(300,20%,60%)  Grey63 */
    rgb(175,135,215), /* 140: #af87d7 hsl(270,50%,68%)  MediumPurple2 */
    rgb(175,135,255), /* 141: #af87ff hsl(260,100%,76%) MediumPurple1 */
    rgb(175,175,0),   /* 142: #afaf00 hsl(60,100%,34%)  Gold3 */
    rgb(175,175,95),  /* 143: #afaf5f hsl(60,33%,52%)   DarkKhaki */
    rgb(175,175,135), /* 144: #afaf87 hsl(60,20%,60%)   NavajoWhite3 */
    rgb(175,175,175), /* 145: #afafaf hsl(0,0%,68%)     Grey69 */
    rgb(175,175,215), /* 146: #afafd7 hsl(240,33%,76%)  LightSteelBlue3 */
    rgb(175,175,255), /* 147: #afafff hsl(240,100%,84%) LightSteelBlue */
    rgb(175,215,0),   /* 148: #afd700 hsl(1,100%,42%)   Yellow3 */
    rgb(175,215,95),  /* 149: #afd75f hsl(80,60%,60%)   DarkOliveGreen3 */
    rgb(175,215,135), /* 150: #afd787 hsl(90,50%,68%)   DarkSeaGreen3 */
    rgb(175,215,175), /* 151: #afd7af hsl(120,33%,76%)  DarkSeaGreen2 */
    rgb(175,215,215), /* 152: #afd7d7 hsl(180,33%,76%)  LightCyan3 */
    rgb(175,215,255), /* 153: #afd7ff hsl(210,100%,84%) LightSkyBlue1 */
    rgb(175,255,0),   /* 154: #afff00 hsl(8,100%,50%)   GreenYellow */
    rgb(175,255,95),  /* 155: #afff5f hsl(90,100%,68%)  DarkOliveGreen2 */
    rgb(175,255,135), /* 156: #afff87 hsl(100,100%,76%) PaleGreen1 */
    rgb(175,255,175), /* 157: #afffaf hsl(120,100%,84%) DarkSeaGreen2 */
    rgb(175,255,215), /* 158: #afffd7 hsl(150,100%,84%) DarkSeaGreen1 */
    rgb(175,255,255), /* 159: #afffff hsl(180,100%,84%) PaleTurquoise1 */
    rgb(215,0,0),     /* 160: #d70000 hsl(0,100%,42%)   Red3 */
    rgb(215,0,95),    /* 161: #d7005f hsl(33,100%,42%)  DeepPink3 */
    rgb(215,0,135),   /* 162: #d70087 hsl(22,100%,42%)  DeepPink3 */
    rgb(215,0,175),   /* 163: #d700af hsl(11,100%,42%)  Magenta3 */
    rgb(215,0,215),   /* 164: #d700d7 hsl(300,100%,42%) Magenta3 */
    rgb(215,0,255),   /* 165: #d700ff hsl(90,100%,50%)  Magenta2 */
    rgb(215,95,0),    /* 166: #d75f00 hsl(6,100%,42%)   DarkOrange3 */
    rgb(215,95,95),   /* 167: #d75f5f hsl(0,60%,60%)    IndianRed */
    rgb(215,95,135),  /* 168: #d75f87 hsl(340,60%,60%)  HotPink3 */
    rgb(215,95,175),  /* 169: #d75faf hsl(320,60%,60%)  HotPink2 */
    rgb(215,95,215),  /* 170: #d75fd7 hsl(300,60%,60%)  Orchid */
    rgb(215,95,255),  /* 171: #d75fff hsl(285,100%,68%) MediumOrchid1 */
    rgb(215,135,0),   /* 172: #d78700 hsl(7,100%,42%)   Orange3 */
    rgb(215,135,95),  /* 173: #d7875f hsl(20,60%,60%)   LightSalmon3 */
    rgb(215,135,135), /* 174: #d78787 hsl(0,50%,68%)    LightPink3 */
    rgb(215,135,175), /* 175: #d787af hsl(330,50%,68%)  Pink3 */
    rgb(215,135,215), /* 176: #d787d7 hsl(300,50%,68%)  Plum3 */
    rgb(215,135,255), /* 177: #d787ff hsl(280,100%,76%) Violet */
    rgb(215,175,0),   /* 178: #d7af00 hsl(8,100%,42%)   Gold3 */
    rgb(215,175,95),  /* 179: #d7af5f hsl(40,60%,60%)   LightGoldenrod3 */
    rgb(215,175,135), /* 180: #d7af87 hsl(30,50%,68%)   Tan */
    rgb(215,175,175), /* 181: #d7afaf hsl(0,33%,76%)    MistyRose3 */
    rgb(215,175,215), /* 182: #d7afd7 hsl(300,33%,76%)  Thistle3 */
    rgb(215,175,255), /* 183: #d7afff hsl(270,100%,84%) Plum2 */
    rgb(215,215,0),   /* 184: #d7d700 hsl(60,100%,42%)  Yellow3 */
    rgb(215,215,95),  /* 185: #d7d75f hsl(60,60%,60%)   Khaki3 */
    rgb(215,215,135), /* 186: #d7d787 hsl(60,50%,68%)   LightGoldenrod2 */
    rgb(215,215,175), /* 187: #d7d7af hsl(60,33%,76%)   LightYellow3 */
    rgb(215,215,215), /* 188: #d7d7d7 hsl(0,0%,84%)     Grey84 */
    rgb(215,215,255), /* 189: #d7d7ff hsl(240,100%,92%) LightSteelBlue1 */
    rgb(215,255,0),   /* 190: #d7ff00 hsl(9,100%,50%)   Yellow2 */
    rgb(215,255,95),  /* 191: #d7ff5f hsl(75,100%,68%)  DarkOliveGreen1 */
    rgb(215,255,135), /* 192: #d7ff87 hsl(80,100%,76%)  DarkOliveGreen1 */
    rgb(215,255,175), /* 193: #d7ffaf hsl(90,100%,84%)  DarkSeaGreen1 */
    rgb(215,255,215), /* 194: #d7ffd7 hsl(120,100%,92%) Honeydew2 */
    rgb(215,255,255), /* 195: #d7ffff hsl(180,100%,92%) LightCyan1 */
    rgb(255,0,0),     /* 196: #ff0000 hsl(0,100%,50%)   Red1 */
    rgb(255,0,95),    /* 197: #ff005f hsl(37,100%,50%)  DeepPink2 */
    rgb(255,0,135),   /* 198: #ff0087 hsl(28,100%,50%)  DeepPink1 */
    rgb(255,0,175),   /* 199: #ff00af hsl(18,100%,50%)  DeepPink1 */
    rgb(255,0,215),   /* 200: #ff00d7 hsl(09,100%,50%)  Magenta2 */
    rgb(255,0,255),   /* 201: #ff00ff hsl(300,100%,50%) Magenta1 */
    rgb(255,95,0),    /* 202: #ff5f00 hsl(2,100%,50%)   OrangeRed1 */
    rgb(255,95,95),   /* 203: #ff5f5f hsl(0,100%,68%)   IndianRed1 */
    rgb(255,95,135),  /* 204: #ff5f87 hsl(345,100%,68%) IndianRed1 */
    rgb(255,95,175),  /* 205: #ff5faf hsl(330,100%,68%) HotPink */
    rgb(255,95,215),  /* 206: #ff5fd7 hsl(315,100%,68%) HotPink */
    rgb(255,95,255),  /* 207: #ff5fff hsl(300,100%,68%) MediumOrchid1 */
    rgb(255,135,0),   /* 208: #ff8700 hsl(1,100%,50%)   DarkOrange */
    rgb(255,135,95),  /* 209: #ff875f hsl(15,100%,68%)  Salmon1 */
    rgb(255,135,135), /* 210: #ff8787 hsl(0,100%,76%)   LightCoral */
    rgb(255,135,175), /* 211: #ff87af hsl(340,100%,76%) PaleVioletRed1 */
    rgb(255,135,215), /* 212: #ff87d7 hsl(320,100%,76%) Orchid2 */
    rgb(255,135,255), /* 213: #ff87ff hsl(300,100%,76%) Orchid1 */
    rgb(255,175,0),   /* 214: #ffaf00 hsl(1,100%,50%)   Orange1 */
    rgb(255,175,95),  /* 215: #ffaf5f hsl(30,100%,68%)  SandyBrown */
    rgb(255,175,135), /* 216: #ffaf87 hsl(20,100%,76%)  LightSalmon1 */
    rgb(255,175,175), /* 217: #ffafaf hsl(0,100%,84%)   LightPink1 */
    rgb(255,175,215), /* 218: #ffafd7 hsl(330,100%,84%) Pink1 */
    rgb(255,175,255), /* 219: #ffafff hsl(300,100%,84%) Plum1 */
    rgb(255,215,0),   /* 220: #ffd700 hsl(0,100%,50%)   Gold1 */
    rgb(255,215,95),  /* 221: #ffd75f hsl(45,100%,68%)  LightGoldenrod2 */
    rgb(255,215,135), /* 222: #ffd787 hsl(40,100%,76%)  LightGoldenrod2 */
    rgb(255,215,175), /* 223: #ffd7af hsl(30,100%,84%)  NavajoWhite1 */
    rgb(255,215,215), /* 224: #ffd7d7 hsl(0,100%,92%)   MistyRose1 */
    rgb(255,215,255), /* 225: #ffd7ff hsl(300,100%,92%) Thistle1 */
    rgb(255,255,0),   /* 226: #ffff00 hsl(60,100%,50%)  Yellow1 */
    rgb(255,255,95),  /* 227: #ffff5f hsl(60,100%,68%)  LightGoldenrod1 */
    rgb(255,255,135), /* 228: #ffff87 hsl(60,100%,76%)  Khaki1 */
    rgb(255,255,175), /* 229: #ffffaf hsl(60,100%,84%)  Wheat1 */
    rgb(255,255,215), /* 230: #ffffd7 hsl(60,100%,92%)  Cornsilk1 */
    rgb(255,255,255), /* 231: #ffffff hsl(0,0%,100%)    Grey100 */
    rgb(8,8,8),       /* 232: #080808 hsl(0,0%,3%)      Grey3 */
    rgb(18,18,18),    /* 233: #121212 hsl(0,0%,7%)      Grey7 */
    rgb(28,28,28),    /* 234: #1c1c1c hsl(0,0%,10%)     Grey11 */
    rgb(38,38,38),    /* 235: #262626 hsl(0,0%,14%)     Grey15 */
    rgb(48,48,48),    /* 236: #303030 hsl(0,0%,18%)     Grey19 */
    rgb(58,58,58),    /* 237: #3a3a3a hsl(0,0%,22%)     Grey23 */
    rgb(68,68,68),    /* 238: #444444 hsl(0,0%,26%)     Grey27 */
    rgb(78,78,78),    /* 239: #4e4e4e hsl(0,0%,30%)     Grey30 */
    rgb(88,88,88),    /* 240: #585858 hsl(0,0%,34%)     Grey35 */
    rgb(98,98,98),    /* 241: #626262 hsl(0,0%,37%)     Grey39 */
    rgb(108,108,108), /* 242: #6c6c6c hsl(0,0%,40%)     Grey42 */
    rgb(118,118,118), /* 243: #767676 hsl(0,0%,46%)     Grey46 */
    rgb(128,128,128), /* 244: #808080 hsl(0,0%,50%)     Grey50 */
    rgb(138,138,138), /* 245: #8a8a8a hsl(0,0%,54%)     Grey54 */
    rgb(148,148,148), /* 246: #949494 hsl(0,0%,58%)     Grey58 */
    rgb(158,158,158), /* 247: #9e9e9e hsl(0,0%,61%)     Grey62 */
    rgb(168,168,168), /* 248: #a8a8a8 hsl(0,0%,65%)     Grey66 */
    rgb(178,178,178), /* 249: #b2b2b2 hsl(0,0%,69%)     Grey70 */
    rgb(188,188,188), /* 250: #bcbcbc hsl(0,0%,73%)     Grey74 */
    rgb(198,198,198), /* 251: #c6c6c6 hsl(0,0%,77%)     Grey78 */
    rgb(208,208,208), /* 252: #d0d0d0 hsl(0,0%,81%)     Grey82 */
    rgb(218,218,218), /* 253: #dadada hsl(0,0%,85%)     Grey85 */
    rgb(228,228,228), /* 254: #e4e4e4 hsl(0,0%,89%)     Grey89 */
    rgb(238,238,238), /* 255: #eeeeee hsl(0,0%,93%)     Grey93 */
#endif
};
#undef rgb

#define REP4(x)   (x), (x), (x), (x)
#define REP5(x)   (x), (x), (x), (x), (x)
#define REP7(x)   REP5(x), (x), (x)
#define REP8(x)   REP4(x), REP4(x)
#define REP9(x)   REP5(x), REP4(x)
#define REP10(x)  REP5(x), REP5(x)
#define REP12(x)  REP10(x), (x), (x)
#define REP13(x)  REP8(x), REP5(x)
#define REP16(x)  REP8(x), REP8(x)
#define REP17(x)  REP16(x), (x)
#define REP20(x)  REP10(x), REP10(x)
#define REP22(x)  REP20(x), (x), (x)
#define REP25(x)  REP20(x), REP5(x)
#define REP40(x)  REP20(x), REP20(x)
#define REP47(x)  REP40(x), REP7(x)

static unsigned char const scale_cube6[256] = {
    /* This array is used for mapping rgb colors to the standard palette
     * 216 entry RGB cube with axes 0,95,135,175,215,255
     */
    REP47(0),   //   0-46  -> 0
    REP47(1),   //  47-93  -> 95
    REP20(1),   //  94-113 -> 95
    REP40(2),   // 114-153 -> 135
    REP40(3),   // 154-193 -> 175
    REP40(4),   // 194-233 -> 215
    REP22(5),   // 234-255 -> 255
};

static unsigned char const scale_cube16[256] = {
    /* This array is used for mapping rgb colors to the 4096 linear palette
     * 4096 entry RGB cube with axes i*17
     */
    REP9(0),    // 0-8 -> 0
    REP17(1),   // 9-25 -> 1
    REP17(2),   // 26-42 -> 2
    REP17(3),   // 43-59 -> 3
    REP17(4),   // 60-76 -> 4
    REP17(5),   // 77-93 -> 5
    REP17(6),   // 94-110 -> 6
    REP17(7),   // 111-127 -> 7
    REP17(8),   // 128-144 -> 8
    REP17(9),   // 145-161 -> 9
    REP17(10),  // 162-178 -> 10
    REP17(11),  // 179-195 -> 11
    REP17(12),  // 196-212 -> 12
    REP17(13),  // 213-229 -> 13
    REP17(14),  // 230-246 -> 14
    REP9(15),   // 247-255 -> 15
};

static unsigned char const scale_grey[256] = {
    /* This array is used for mapping gray levels to
     * the xterm extended palette
     * 232..255: 24 entry grey scale 8,18..238
     * also map to the 216 cube diagonal:
     * entries 16,59,102,145,188,231
     *  values  0,95,135,175,215,255
     */
    REP4(16),       //   0-3   -> 0
    REP10(232),     //   4-13  -> 8
    REP10(233),     //  14-23  -> 18
    REP10(234),     //  24-33  -> 28
    REP10(235),     //  34-43  -> 38
    REP10(236),     //  44-53  -> 48
    REP10(237),     //  54-63  -> 58
    REP10(238),     //  64-73  -> 68
    REP10(239),     //  74-83  -> 78
    REP8(240),      //  84-91  -> 88
    REP5(59),       //  92-96  -> 95
    REP7(241),      //  97-103 -> 98
    REP10(242),     // 104-113 -> 108
    REP10(243),     // 114-123 -> 118
    REP8(244),      // 124-131 -> 128
    REP5(102),      // 132-136 -> 135
    REP7(245),      // 137-143 -> 138
    REP10(246),     // 144-153 -> 148
    REP10(247),     // 154-163 -> 158
    REP8(248),      // 164-171 -> 168
    REP5(145),      // 172-176 -> 175
    REP7(249),      // 177-183 -> 178
    REP10(250),     // 184-193 -> 188
    REP10(251),     // 194-203 -> 198
    REP8(252),      // 204-211 -> 208
    REP5(188),      // 212-216 -> 208
    REP7(253),      // 217-223 -> 218
    REP10(254),     // 224-233 -> 228
    REP13(255),     // 234-246 -> 238
    REP9(231),      // 247-255 -> 255
};

int color_dist(QEColor c1, QEColor c2) {
    /* using casts because c1 and c2 are unsigned */
#if 0
    /* using a quick approximation to give green extra weight */
    return      abs((int)((c1 >>  0) & 0xff) - (int)((c2 >>  0) & 0xff)) +
            2 * abs((int)((c1 >>  8) & 0xff) - (int)((c2 >>  8) & 0xff)) +
                abs((int)((c1 >> 16) & 0xff) - (int)((c2 >> 16) & 0xff));
#else
    /* this computes the d-inf distance in the RGB cube with extra weight
       for green and red consistent with the YCbCr coordinates
     */
    return  11 * abs((int)((c1 >>  0) & 0xff) - (int)((c2 >>  0) & 0xff)) +
            59 * abs((int)((c1 >>  8) & 0xff) - (int)((c2 >>  8) & 0xff)) +
            30 * abs((int)((c1 >> 16) & 0xff) - (int)((c2 >> 16) & 0xff));
#endif
}

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
            /* mapping to a 16-color palette: use brute force
             * to minimize the weighted RGB distance
             */
            for (i = 0; i < count; i++) {
                d = color_dist(color, colors[i]);
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                }
            }
        } else { /* if (count > 16) */
            /* More general case: 256 or 8192 palette:
             * Using the terminal 256 color palette causes gliches on
             * true color terminals with a non standard xterm palette,
             * such as iTerm2.
             * On true color terminals, we should treat palette
             * colors and RGB colors differently in the shell buffer
             * terminal emulator.
             */
            int r = (color >> 16) & 0xff;
            int g = (color >>  8) & 0xff;
            int b = (color >>  0) & 0xff;
#if 0
            for (i = 16; i < 256; i++) {
                d = color_dist(color, colors[i]);
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                    if (d == 0)
                        break;
                }
            }
#else
            if (r == g && g == b) {
                /* color is a gray tone:
                 * map to the closest palette entry in the grey ramp.
                 * do not use colors 0,7,8,15 because they tend to differ
                 * from one terminal to another.
                 */
                // FIXME: use this method for almost greys (eg: #FEFFFE)
                int grey = r;
                cmin = scale_grey[grey];
                dmin = color_dist(color, colors[cmin]);
                if (dmin > 0 && count >= 4096) {
                    cmin = 0x700 + grey;
                    dmin = 0;
                }
            } else {
                /* general case: try and match a palette entry
                 * from the 6x6x6 color cube. This may be a problem if the
                 * terminal colors 16-231 use different scales (iTerm2).
                 */
                int r1 = scale_cube6[r];
                int g1 = scale_cube6[g];
                int b1 = scale_cube6[b];
                cmin = 16 + r1 * 36 + g1 * 6 + b1;
                dmin = color_dist(color, colors[cmin]);
            }
#endif
            if (dmin > 0 && count >= 4096) {
                /* 13-bit 7936 color system */
                QEColor rgb;

                d = 0;
                for (;;) {
                    if (r == g) {
                        i = r + 0x700; if (g == b)   break; /* #xxxxxx */
                        i = b + 0x100; if (r == 0)   break; /* #0000xx */
                        i = b + 0x900; if (r == 255) break; /* #FFFFxx */
                        i = r + 0x600; if (b == 0)   break; /* #xxxx00 */
                        i = r + 0xE00; if (b == 255) break; /* #xxxxFF */
                    } else
                    if (b == r) {
                        i = g + 0x200; if (r == 0)   break; /* #00xx00 */
                        i = g + 0xA00; if (r == 255) break; /* #FFxxFF */
                        i = b + 0x500; if (g == 0)   break; /* #xx00xx */
                        i = b + 0xD00; if (g == 255) break; /* #xxFFxx */
                    } else
                    if (g == b) {
                        i = r + 0x400; if (g == 0)   break; /* #xx0000 */
                        i = r + 0xC00; if (g == 255) break; /* #xxFFFF */
                        i = g + 0x300; if (r == 0)   break; /* #00xxxx */
                        i = g + 0xB00; if (r == 255) break; /* #FFxxxx */
                    }
                    if ((1)) {
                        /* Try color in the linear 16x16x16 RGB cube */
                        int r1 = scale_cube16[r];
                        int g1 = scale_cube16[g];
                        int b1 = scale_cube16[b];
                        i = 0x1000 + (r1 << 8) + (g1 << 4) + b1;
                        rgb = QERGB(17 * r1, 17 * g1, 17 * b1);
                        d = color_dist(color, rgb);
                        break;
                    }
                }
                if (d < dmin) {
                    cmin = i;
                    dmin = d;
                }
                if (dmin) {
                    for (i = 0; i < nb_custom_colors; i++) {
                        if (custom_colors[i] == color) {
                            cmin = i + 0x800 + (i & 256) * 6;
                            dmin = 0;
                            break;
                        }
                    }
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
        /* XXX: returns an alpha channel of 0xFF */
        return xterm_colors[color];
    }
    if (color < 8192) {
        /* 13-bit 7936 color system */
        if (color & 0x1000) {
            /* explicit 12-bit color */
            // FIXME: should try alternate approach using
            //        5 bits for green and 3 bits for blue
            QEColor rgb = (((color & 0xF00) << 8) |
                           ((color & 0x0F0) << 4) |
                           ((color & 0x00F) << 0));
            /* XXX: returns an alpha channel of 0xFF */
            return 0xFF000000 | rgb | (rgb << 4);
        }
        if ((color & 0x700) == 0) {
            /* 0x800 indicates a custom palette entry */
            return custom_colors[color & 255] | 0xFF000000;
        }
        if ((color & 0xf00) < 0xf00) {
            /* 14 256-level color ramps */
            int r, g, b, other;
            r = g = b = color & 0xFF;
            other = -(color >> 11) & 0xFF;
            if (!(color & 0x400)) r = other;
            if (!(color & 0x200)) g = other;
            if (!(color & 0x100)) b = other;
            return QERGB(r, g, b);
        } else {
            /* 0xf00 indicates the second half of the custom palette */
            return custom_colors[color - 0xe00] | 0xFF000000;
        }
    }
    /* explicit RGB color with full alpha channel */
    return color | 0xFF000000;
}

static int add_custom_color(QEColor color) {
    int dist, index = -1;
    if (color != COLOR_TRANSPARENT) {
        qe_map_color(color, xterm_colors, 8192, &dist);
        if (dist && nb_custom_colors < countof(custom_colors)) {
            index = nb_custom_colors++;
            custom_colors[index] = color & 0x00FFFFFF;
        }
    }
    return index;
}

int colors_init(void) {
    int i;
    for (i = 0; i < nb_default_colors; i++) {
        /* register colors in the custom palette (if not found) */
        add_custom_color(default_colors[i].color);
    }
    return 0;
}

static int css_lookup_color(ColorDef const *def, int count,
                            const char *name)
{
    char buf[32];
    const char *grey;
    int i;

    for (;;) {
        // FIXME: should sort the table and use binary search
        for (i = 0; i < count; i++) {
            if (!strxcmp(def[i].name, name))
                return i;
        }
        if (name == buf)
            break;
        if ((grey = strstr(name, "grey")) != NULL) {
            pstrcpy(buf, sizeof buf, name);
            buf[grey - name + 2] = 'a';
        } else
        if ((grey = strstr(name, "gray")) != NULL) {
            pstrcpy(buf, sizeof buf, name);
            buf[grey - name + 2] = 'e';
        } else {
            break;
        }
        name = buf;
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
        def = qe_malloc_dup_array(default_colors, nb_default_colors);
        if (!def)
            return -1;
        qe_colors = def;
    }
    /* Make room: reallocate table in chunks of 8 entries */
    // FIXME: this will reallocate the table even if the color exists
    if (((nb_qe_colors - nb_default_colors) & 7) == 0) {
        if (!qe_realloc_array(&qe_colors, nb_qe_colors + 8))
            return -1;
    }
    /* Check for redefinition of color name */
    index = css_lookup_color(qe_colors, nb_qe_colors, name);
    if (index < 0) {
        index = nb_qe_colors++;
        qe_colors[index].name = qe_strdup(name);
    }
    qe_colors[index].color = color;
    /* Add custom color if not already in the palette */
    add_custom_color(color);
    return 0;
}

const char *css_get_color_name(char *dest, size_t size, QEColor color, int lookup) {
    if (lookup) {
        int i;
        for (i = 0; i < nb_qe_colors; i++) {
            if (qe_colors[i].color == color)
                return qe_colors[i].name;
        }
    }
    if (color == COLOR_TRANSPARENT)
        return "transparent";
    snprintf(dest, size, "#%06x", color & 0xFFFFFF);
    return dest;
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
    const char *q;

    rgba[3] = 0xff;
    if (*p == '#') {
        /* handle '#' notation */
        p++;
    parse_num:
        for (len = 0; qe_isxdigit(p[len]); len++)
            continue;
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
    if (*p == 'p' && qe_isdigit(p[1])) {
        QEColor rgb;
        v = strtol_c(p + 1, &p, 0);
        rgb = qe_unmap_color(v, 8192);
        rgba[0] = (rgb >> 16) & 255;
        rgba[1] = (rgb >> 8) & 255;
        rgba[2] = (rgb >> 0) & 255;
        rgba[3] = (rgb >> 24) & 255;
    } else
    if (strstart(p, "rgb:", &p)) {
        /* parse XParseColor syntax */
        for (i = 0; i < 3; i++) {
            v = strtol_c(q = p, &p, 16);
            len = p - q;
            if (len < 2)
                v |= (v << 4);
            while (len --> 2)
                v >>= 4;
            rgba[i] = v | (v << 4);
            if (*p == '/')
                p++;
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
        /* search in tables */
        def = qe_colors;
        count = nb_qe_colors;

        index = css_lookup_color(def, count, p);
        if (index >= 0) {
            *color_ptr = def[index].color;
            return 0;
        }

        if (qe_isxdigit((unsigned char)*p))
            goto parse_num;

        // FIXME: accept hsv(), hsva(), hsl(), hsla()...
        // FIXME: accept XParseColor syntax rgb:x/x/x
        return -1;
    }
    *color_ptr = ((rgba[0] << 16) | (rgba[1] << 8) |
                  (rgba[2]) | ((uint32_t)rgba[3] << 24));
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
