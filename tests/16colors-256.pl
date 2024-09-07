#!/usr/bin/perl
#
# Copyright (c) 2002-2017 Charlie Gordon.
#
# use the resources for colors 0-15 - usually more-or-less a
# reproduction of the standard ANSI colors, but possibly more
# pleasing shades

# display the colors using CSI 48;5;palette number m

# first the system ones:
print "Background colors:\n";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[48;5;%dm %02d ", $color, $color);
    # print "\x1b[${n}m    ";
}
print "\x1b[0m\n\n";

print "Blinking colors:\n";
print "\x1b[5m";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[48;5;%dm %02d ", $color, $color);
    # print "\x1b[${n}m    ";
}
print "\x1b[0m\n\n";

print "Foreground colors:\n";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[38;5;%dm %02d ", $color, $color);
}
print "\x1b[0m\n\n";

print "Bold colors:\n";
print "\x1b[1m";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[38;5;%dm %02d ", $color, $color);
}
print "\x1b[0m\n\n";

print "Italic colors:\n";
print "\x1b[3m";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[38;5;%dm %02d ", $color, $color);
}
print "\x1b[0m\n\n";

print "Bold+Italic colors:\n";
print "\x1b[1m\x1b[3m";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[38;5;%dm %02d ", $color, $color);
}
print "\x1b[0m\n\n";

print "Underline colors:\n";
print "\x1b[4m";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[38;5;%dm %02d ", $color, $color);
}
print "\x1b[0m\n\n";

print "Reverse colors:\n";
print "\x1b[7m";
for ($color = 0; $color < 16; $color++) {
    printf("\x1b[38;5;%dm %02d ", $color, $color);
}
print "\x1b[0m\n\n";

print "Color combinations:\n";
for ($bg = 0; $bg < 16; $bg++) {
    printf("\x1b[48;5;%dm", $bg);
    # printf("\x1b[%dm", ($color > 7) ? 5 : 25);
    # printf("\x1b[%dm", 40 + ($color & 7));
    for ($fg = 0; $fg < 16; $fg++) {
        printf("\x1b[38;5;%dm %02d ", $fg, $fg);
        # printf("\x1b[%dm", ($fg > 7) ? 1 : 22);
        # printf("\x1b[%dm %02d ", 30 + ($fg & 7), $fg);
    }
    print "\x1b[0m\n";
}
print "\x1b[0m\n";
