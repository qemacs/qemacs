#!/usr/bin/perl
#
# Copyright (c) 2002-2017 Charlie Gordon.
#
# use the resources for colors 0-15 - usually more-or-less a
# reproduction of the standard ANSI colors, but possibly more
# pleasing shades

# display the colors using CSI 3x/4x/9x/10x m sequences

# first the system ones:
print "Background colors:\n";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 100 + $color - 8 : $color + 40;
    printf("\x1b[%dm %02d ", $n, $color);
    # print "\x1b[${n}m    ";
}
print "\x1b[0m\n\n";

print "Blinking colors:\n";
print "\x1b[5m";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 100 + $color - 8 : $color + 40;
    printf("\x1b[%dm %02d ", $n, $color);
    # print "\x1b[${n}m    ";
}
print "\x1b[0m\n\n";

print "Foreground colors:\n";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 90 + $color - 8 : $color + 30;
    printf("\x1b[%dm %02d ", $n, $color);
}
print "\x1b[0m\n\n";

print "Bold colors:\n";
print "\x1b[1m";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 90 + $color - 8 : $color + 30;
    printf("\x1b[%dm %02d ", $n, $color);
}
print "\x1b[0m\n\n";

print "Italic colors:\n";
print "\x1b[3m";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 90 + $color - 8 : $color + 30;
    printf("\x1b[%dm %02d ", $n, $color);
}
print "\x1b[0m\n\n";

print "Bold+Italic colors:\n";
print "\x1b[1m\x1b[3m";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 90 + $color - 8 : $color + 30;
    printf("\x1b[%dm %02d ", $n, $color);
}
print "\x1b[0m\n\n";

print "Underline colors:\n";
print "\x1b[4m";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 90 + $color - 8 : $color + 30;
    printf("\x1b[%dm %02d ", $n, $color);
}
print "\x1b[0m\n\n";

print "Reverse colors:\n";
print "\x1b[7m";
for ($color = 0; $color < 16; $color++) {
    $n = $color > 7 ? 90 + $color - 8 : $color + 30;
    printf("\x1b[%dm %02d ", $n, $color);
}
print "\x1b[0m\n\n";

print "Color combinations:\n";
for ($bg = 0; $bg < 16; $bg++) {
    $n = $bg > 7 ? 100 + $bg - 8 : $bg + 40;
    # printf("\x1b[%dm", ($color > 7) ? 5 : 25);
    # printf("\x1b[%dm", 40 + ($color & 7));
    printf("\x1b[%dm", $n);
    for ($fg = 0; $fg < 16; $fg++) {
        $n = $fg > 7 ? 90 + $fg - 8 : $fg + 30;
        # printf("\x1b[%dm", ($fg > 7) ? 1 : 22);
        # printf("\x1b[%dm %02d ", 30 + ($fg & 7), $fg);
        printf("\x1b[%dm %02d ", $n, $fg);
    }
    print "\x1b[0m\n";
}
print "\x1b[0m\n";
