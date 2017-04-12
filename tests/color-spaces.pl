#!/usr/bin/perl
# Author: Todd Larason <jtl@molehill.org>
# $XFree86: xc/programs/xterm/vttests/256colors2.pl,v 1.1 1999/07/11 08:49:54 dawes Exp $

print "256 color mode\n\n";

# display foreground and background colors

for ($fgbg = 38; $fgbg <= 48; $fgbg +=10) {

    # first the system ones:
    print "System colors:\n";
    for ($color = 0; $color < 8; $color++) {
        print "\x1b[${fgbg};5;${color}m::";
    }
    print "\x1b[0m\n";
    for ($color = 8; $color < 16; $color++) {
        print "\x1b[${fgbg};5;${color}m::";
    }
    print "\x1b[0m\n\n";

    # now the color cube
    print "Color cube, 6x6x6:\n";
    for ($green = 0; $green < 6; $green++) {
        for ($red = 0; $red < 6; $red++) {
            for ($blue = 0; $blue < 6; $blue++) {
                $color = 16 + ($red * 36) + ($green * 6) + $blue;
                print "\x1b[${fgbg};5;${color}m::";
            }
            print "\x1b[0m ";
        }
        print "\n";
    }

    # now the grayscale ramp
    print "Grayscale ramp:\n";
    print "\x1b[${fgbg};5;16m::";
    for ($color = 232; $color < 256; $color++) {
        print "\x1b[${fgbg};5;${color}m::";
    }
    print "\x1b[${fgbg};5;231m::";
    print "\x1b[0m\n\n";
}

if (0) {
print "Examples for the 3-byte color mode\n\n";

for ($fgbg = 38; $fgbg <= 48; $fgbg +=10) {
    # now the color cube
    print "Color cube\n";
    for ($green = 0; $green < 256; $green += 51) {
        for ($red = 0; $red < 256; $red += 51) {
            for ($blue = 0; $blue < 256; $blue += 51) {
                print "\x1b[${fgbg};2;${red};${green};${blue}m::";
            }
            print "\x1b[0m ";
        }
        print "\n";
    }
    
    # now the grayscale ramp
    print "Grayscale ramp:\n";
    print "\x1b[${fgbg};2;0;0;0m::";
    for ($gray = 8; $gray < 256; $gray += 10) {
        print "\x1b[${fgbg};2;${gray};${gray};${gray}m::";
    }
    print "\x1b[${fgbg};2;255;255;255m::";
    print "\x1b[0m\n\n";
}
}

print "Compare direct palette and RGB color modes\n\n";

$s = "::";

for ($fgbg = 48; $fgbg <= 48; $fgbg +=10) {
    # the color cube
    print "RGB color cube\n";
    for ($green = 0; $green < 6; $green += 1) {
        for ($red = 0; $red < 6; $red += 1) {
            for ($blue = 0; $blue < 6; $blue += 1) {
                $r = $red * 51;
                $g = $green * 51;
                $b = $blue * 51;
                print "\x1b[${fgbg};2;${r};${g};${b}m${s}";
            }
            print "\x1b[0m ";
        }
        print "\n";
    }
    print "Xterm palette color cube\n";
    for ($green = 0; $green < 6; $green += 1) {
        for ($red = 0; $red < 6; $red += 1) {
            for ($blue = 0; $blue < 6; $blue += 1) {
                $r = $red ? $red * 40 + 55 : 0;
                $g = $green ? $green * 40 + 55 : 0;
                $b = $blue ? $blue * 40 + 55 : 0;
                print "\x1b[${fgbg};2;${r};${g};${b}m${s}";
            }
            print "\x1b[0m ";
        }
        print "\n";
    }
    print "Local palette color cube\n";
    for ($green = 0; $green < 6; $green += 1) {
        for ($red = 0; $red < 6; $red += 1) {
            for ($blue = 0; $blue < 6; $blue += 1) {
                $color = 16 + $red * 36 + $green * 6 + $blue;
                print "\x1b[${fgbg};5;${color}m${s}";
            }
            print "\x1b[0m ";
        }
        print "\n";
    }
    
    # the grayscale ramp
    print "Grayscale ramp:\n";
    print "    RGB: ";
    print "\x1b[${fgbg};2;0;0;0m${s}";
    for ($gray = 8; $gray < 256; $gray += 10) {
        print "\x1b[${fgbg};2;${gray};${gray};${gray}m${s}";
    }
    print "\x1b[${fgbg};2;255;255;255m${s}";
    print "\x1b[0m\n";
    print "Palette: ";
    print "\x1b[${fgbg};2;0;0;0m${s}";
    for ($gray = 8; $gray < 256; $gray += 10) {
        print "\x1b[${fgbg};2;${gray};${gray};${gray}m${s}";
    }
    print "\x1b[${fgbg};2;255;255;255m${s}";
    print "\x1b[0m\n";
    print "\n";
}

if (0) {
    $fgbg = 48;
    # debug the color cube
    print "Color cube\n";
    for ($green = 0; $green < 256; $green += 51) {
        for ($red = 0; $red < 256; $red += 51) {
            for ($blue = 0; $blue < 256; $blue += 51) {
                print "\x1b[0mr:${red}\tg:${green}:\tb:${blue}\t";
                print "c:${color}\t";
                print "\x1b[${fgbg};2;${red};${green};${blue}m::";
                print "  xxXX\x1b[0m";
                $color = 16 + ($red / 51) * 36 + ($green / 51) * 6 + ($blue / 51);
                print " \x1b[${fgbg};5;${color}m::";
                print "  xxXX\x1b[0m";
                print "\n";
            }
            print "\n";
        }
        print "\n";
    }
}
