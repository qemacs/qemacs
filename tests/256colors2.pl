#!/usr/bin/perl
# Author: Todd Larason <jtl@molehill.org>
# $XFree86: xc/programs/xterm/vttests/256colors2.pl,v 1.2 2002/03/26 01:46:43 dickey Exp $

# use the resources for colors 0-15 - usually more-or-less a
# reproduction of the standard ANSI colors, but possibly more
# pleasing shades

# rgb cube axis is 40*comp + 55 except for 0
@rgbaxis = (0,95,135,175,215,255);
# gray scale intentionally leaves out black and white
@grayaxis = (8,18,28,38,48,58,68,78,88,98,108,118,128,138,148,158,168,178,188,198,208,218,228,238,248);

if (false) {
    # redefine color 16 to 255
    # colors 16-231 are a 6x6x6 color cube
    $n = 16;
    for ($red = 0; $red < 6; $red++) {
        for ($green = 0; $green < 6; $green++) {
            for ($blue = 0; $blue < 6; $blue++, $n++) {
                printf("\x1b]4;%d;rgb:%2.2x/%2.2x/%2.2x\x1b\\",
                       $n, $rgbaxis[$red], $rgbaxis[$green], $rgbaxis[$blue]);
            }
        }
    }

    # colors 232-255 are a grayscale ramp, intentionally leaving out
    # black and white
    $n = 232;
    for ($gray = 0; $gray < 24; $gray++, $n++) {
        $level = ($gray * 10) + 8;
        printf("\x1b]4;%d;rgb:%2.2x/%2.2x/%2.2x\x1b\\",
               $n, $level, $level, $level);
    }
}

# display the colors
$height = 2;
# first the system ones:
print "System colors:\n";
if ($height != 2) {
    for ($color = 0; $color < 8; $color++) {
        printf(" %02d ", $color);
    }
    print "\n";
}
for ($i = 0; $i < $height; $i++) {
    for ($color = 0; $color < 8; $color++) {
        $textcolor = ($color == 0 || $color == 8) ? 7 : 0;
        if ($i + 2 == $height) {
            printf("\x1b[38;5;${color}m %02d ", $color);
        } else {
            printf("\x1b[38;5;%dm", $textcolor);
            printf("\x1b[48;5;%dm %02d ", $color, $color);
        }
    }
    print "\x1b[0m\n";
}
if ($height != 2) {
    for ($color = 8; $color < 16; $color++) {
        printf(" %02d ", $color);
    }
    print "\n";
}
for ($i = 0; $i < $height; $i++) {
    for ($color = 8; $color < 16; $color++) {
        $textcolor = ($color == 0 || $color == 8) ? 7 : 0;
        if ($i + 2 == $height) {
            printf("\x1b[38;5;${color}m %02d ", $color);
        } else {
            printf("\x1b[38;5;%dm", $textcolor);
            printf("\x1b[48;5;%dm %02d ", $color, $color);
        }
    }
    print "\x1b[0m\n";
}
print "\n";

# now the color cube
print "Color cube, 6x6x6:\n";
for ($green = 0; $green < 6; $green++) {
    for ($i = 0; $i < $height; $i++) {
        for ($red = 0; $red < 3; $red++) {
            for ($blue = 0; $blue < 6; $blue++) {
                $color = 16 + ($red * 36) + ($green * 6) + $blue;
                $y = 20 * $rgbaxis[$red] + 59 * $rgbaxis[$green] + 11 * $rgbaxis[$blue];
                $textcolor = $y >= 12800 ? 0 : 15;
                if ($i + 2 == $height) {
                    printf("\x1b[38;5;%dm%4d", $color, $color);
                } else {
                    printf("\x1b[38;5;%dm", $textcolor);
                    printf("\x1b[48;5;%dm%4d", $color, $color);
                }
            }
            print "\x1b[0m ";
        }
        print "\n";
    }
}
print "\n";
for ($green = 0; $green < 6; $green++) {
    for ($i = 0; $i < $height; $i++) {
        for ($red = 3; $red < 6; $red++) {
            for ($blue = 0; $blue < 6; $blue++) {
                $color = 16 + ($red * 36) + ($green * 6) + $blue;
                #$textcolor = 16 + (($red + 3) % 6 * 36) + (($green + 3) % 6 * 6) + ($blue + 3) % 6;
                $y = 20 * $rgbaxis[$red] + 59 * $rgbaxis[$green] + 11 * $rgbaxis[$blue];
                $textcolor = $y >= 12800 ? 0 : 15;
                if ($i + 2 == $height) {
                    printf("\x1b[38;5;%dm%4d", $color, $color);
                } else {
                    printf("\x1b[38;5;%dm", $textcolor);
                    printf("\x1b[48;5;%dm%4d", $color, $color);
                }
            }
            print "\x1b[0m";
            if ($red < 5) { print " "; }
        }
        print "\n";
    }
}
print "\n";

# now the grayscale ramp
print "Grayscale ramp:\n";
for ($i = 0; $i < $height; $i++) {
    for ($color = 232; $color < 244; $color++) {
        if ($i + 2 == $height) {
            printf("\x1b[38;5;${color}m%4d", $color);
        } else {
            $y = 100 * $grayaxis[$color - 232];
            $textcolor = $y >= 12800 ? 0 : 15;
            printf("\x1b[38;5;%dm", $textcolor);
            printf("\x1b[48;5;%dm%4d", $color, $color);
        }
    }
    print "\x1b[0m\n";
}
for ($i = 0; $i < $height; $i++) {
    for ($color = 244; $color < 256; $color++) {
        if ($i + 2 == $height) {
            printf("\x1b[38;5;${color}m%4d", $color);
        } else {
            $y = 100 * $grayaxis[$color - 232];
            $textcolor = $y >= 12800 ? 0 : 15;
            printf("\x1b[38;5;%dm", $textcolor);
            printf("\x1b[48;5;%dm%4d", $color, $color);
        }
    }
    print "\x1b[0m\n";
}
