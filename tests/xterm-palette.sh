#!/bin/bash

resetColor() {
    printf '\x1b[0m%s' "$1";
}

paletteColor() {
    printf '\x1b[48;5;%sm%s' $1 "$2";
}

rgbColor() {
    printf '\x1b[48;2;%s;%s;%sm%s' $1 $2 $3 "$4";
}

paletteColorRGB() {
    let index=$1;
    export rgb="0 0 0";

    case $1 in
       0 ) export rgb="$[0x00] $[0x00] $[0x00]" ;;
       1 ) export rgb="$[0xbb] $[0x00] $[0x00]" ;;
       2 ) export rgb="$[0x00] $[0xbb] $[0x00]" ;;
       3 ) export rgb="$[0xbb] $[0xbb] $[0x00]" ;;
       4 ) export rgb="$[0x00] $[0x00] $[0xbb]" ;;
       5 ) export rgb="$[0xbb] $[0x00] $[0xbb]" ;;
       6 ) export rgb="$[0x00] $[0xbb] $[0xbb]" ;;
       7 ) export rgb="$[0xbb] $[0xbb] $[0xbb]" ;;

       8 ) export rgb="$[0x55] $[0x55] $[0x55]" ;;
       9 ) export rgb="$[0xff] $[0x55] $[0x55]" ;;
      10 ) export rgb="$[0x55] $[0xff] $[0x55]" ;;
      11 ) export rgb="$[0xff] $[0xff] $[0x55]" ;;
      12 ) export rgb="$[0x55] $[0x55] $[0xff]" ;;
      13 ) export rgb="$[0xff] $[0x55] $[0xff]" ;;
      14 ) export rgb="$[0x55] $[0xff] $[0xff]" ;;
      15 ) export rgb="$[0xff] $[0xff] $[0xff]" ;;
       * )

        if [ $index -ge 16 -a $index -lt 232 ] ; then
            # xterm: 0,95,135,175,215,255
            let i=index-16;
            let r="(i / 36) ? ((i / 36) * 40 + 55) : 0";
            let g="(i % 36) / 6 ? (((i % 36) / 6) * 40 + 55) : 0";
            let b="(i % 6) ? ((i % 6) * 40 + 55) : 0";
            export rgb="$r $g $b";
        elif [ $index -ge 232 -a $index -le 255 ] ; then
            let n="index-232";
            let g="8+n*10";
            export rgb="$g $g $g";
        else
            resetColor $'\n' ; echo "Invalid palette color $1" ; exit 1
        fi ;;
    esac
    rgbColor $rgb "$2"
}

xtermLevel() {
    # 0,95,135,175,215,255
    # vim: 0,95,135,175,215,255 (00 5f 87 af d7 ff)
    # emacs? 55,95,135,175,215,255
    # macOS? 22,95,135,175,215,255

    # if (index >= 16 && index < 232) {
        # int i = index - 16;
        # r = (i / 36) ? ((i / 36) * 40 + 55) / 255.0 : 0.0;
        # g = (i % 36) / 6 ? (((i % 36) / 6) * 40 + 55) / 255.0 : 0.0;
        # b = (i % 6) ? ((i % 6) * 40 + 55) / 255.0 : 0.0;

    case $1 in
      0 ) echo  '22' ; return ;;
      1 ) echo  '95' ; return ;;
      2 ) echo '135' ; return ;;
      3 ) echo '175' ; return ;;
      4 ) echo '215' ; return ;;
      5 ) echo '255' ; return ;;
      * ) resetColor $'\n' ; echo "Invalid xtermLevel $1" ; exit 1 ;;
    esac
}

ramp() {
    for n in `seq $[$1+$2] $[$1+$3-1]`; do
        paletteColor $n "$4";
    done
    resetColor $'\n';
    for n in `seq $[$1+$2] $[$1+$3-1]`; do
        paletteColorRGB $n "$4";
    done
#    if [ $1 == 0 ] ; then
#        # System colors
#        for n in `seq $[$2] $[$3-1]`; do
#            paletteColor $n "$4";
#        done
#    elif [ $1 == 16 ] ; then
#        # Color cube
#        for n in `seq $[$2] $[$3-1]`; do
#            let r=$(xtermLevel $[$n/36])
#            let g=$(xtermLevel $[$n/6%6])
#            let b=$(xtermLevel $[$n%6])
#            rgbColor $r $g $b "$4";
#        done
#    else
#        # Gray scale
#        for n in `seq $[$2] $[$3-1]`; do
#            let g="8+n*10"
#            rgbColor $g $g $g "$4";
#        done
#    fi
    resetColor $'\n';
}

{
    export space="   ";
    if [ "$1" == "-w" ] ; then export space="    "; shift; fi

    printf "System colors:\n";
    ramp   0   0  16 "$space";
    printf "Color cube, 6x6x6:\n";
    ramp  16   0  18 "$space";
    ramp  16  36  54 "$space";
    ramp  16  72  90 "$space";
    ramp  16 108 126 "$space";
    ramp  16 144 162 "$space";
    ramp  16 180 198 "$space";
    ramp  16  18  36 "$space";
    ramp  16  54  72 "$space";
    ramp  16  90 108 "$space";
    ramp  16 126 144 "$space";
    ramp  16 162 180 "$space";
    ramp  16 198 216 "$space";
    printf "Grayscale ramp:\n";
    ramp 232   0  24 "$space";
}
