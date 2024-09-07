#!/usr/bin/env bash
#
#   This file tests support for the 7936 color system
#   a full 4x4x4 4096-color cube is drawn using 24-bit
#   set background color escape sequences.
#   various color ramps are drawn:
#    - 256 level fade to back from pure colors
#    - 256 level fade to white from pure colors
#    - 256 level gray scales
#    - 256 shade fully saturated rainbow colors

#export out=""

check="$1"

output() {
    echo -ne "$1"
    #export out="$out$1"
}

flush() {
    true;
    #echo -ne "$out"
}

setBackgroundColor() {
    # printf '\x1bPtmux;\x1b\x1b[48;2;%s;%s;%sm' $1 $2 $3
    # printf '\x1b[48;2;%s;%s;%sm%s' $1 $2 $3 "$4"
    #echo -ne "\033[48;2;$1;$2;$3m$4"
    let rr="$1"
    let gg="$2"
    let bb="$3"
    output "\033[48;2;${rr};${gg};${bb}m$4"
}

resetColor() {
    # printf '\x1b[0m%s' "$1"
    #echo -ne "\033[0m$1"
    output "\033[0m$1"
}

# Gives a color $1/255 % along HSV
# Who knows what happens when $1 is outside 0-255
# Echoes "$red $green $blue" where
# $red $green and $blue are integers
# ranging between 0 and 255 inclusive
rainbowColor() {
    let h=$1/43
    let f=$1-43*$h
    let t=$f*255/43
    let q=255-t

    if [ $h -eq 0 ]
    then
        echo "255 $t 0"
    elif [ $h -eq 1 ]
    then
        echo "$q 255 0"
    elif [ $h -eq 2 ]
    then
        echo "0 255 $t"
    elif [ $h -eq 3 ]
    then
        echo "0 $q 255"
    elif [ $h -eq 4 ]
    then
        echo "$t 0 255"
    elif [ $h -eq 5 ]
    then
        echo "255 0 $q"
    else
        # execution should never reach here
        echo "0 0 0"
    fi
}

paletteRamp() {
    let end=$1+$2-1
    for i in `seq $1 $end`; do
        output "\033[48;5;${i}m$3"
    done
    resetColor "$4";
}

paletteEmulate() {
    let end=$1+$2-1
    for i in `seq $1 $end`; do
        let rr6="$i/36%6"
        let gg6="$i/6%6"
        let bb6="$i%6"
        let rr="$rr6?$rr6*40+55:0"
        let gg="$gg6?$gg6*40+55:0"
        let bb="$bb6?$bb6*40+55:0"
        output "\033[48;2;${rr};${gg};${bb}m$3"
    done
    resetColor $'\n';
}

colorRamp() {
    for i in `seq 0 255`; do
        if [ $i -lt 64 ] ; then let v=$i ;
        elif [ $i -lt 128 ] ; then let v=192-$i ;
        elif [ $i -lt 192 ] ; then let v=$i ;
        else let v=255+192-$i ; fi
        let r=$v*$1
        let g=$v*$2
        let b=$v*$3
        setBackgroundColor $r $g $b "$4"
        if [ $i -eq 63 ] ; then resetColor $'\n' ; fi
        if [ $i -eq 127 ] ; then resetColor $'\n' ; fi
        if [ $i -eq 191 ] ; then resetColor $'\n' ; fi
    done
    resetColor $'\n'
}

colorRamp1() {
    for i in `seq 0 255`; do
        if [ $i -lt 128 ] ; then let v=i ; else let v=255+128-i ; fi
        let r=$v*$1
        let g=$v*$2
        let b=$v*$3
        setBackgroundColor $r $g $b "$4"
        if [ $i -eq 127 ] ; then resetColor $'\n' ; fi
    done
    resetColor $'\n'
}

colorRamp2() {
    for i in `seq 0 255`; do
        if [ $i -lt 64 ] ; then let v=$i ;
        elif [ $i -lt 128 ] ; then let v=192-$i ;
        elif [ $i -lt 192 ] ; then let v=$i ;
        else let v=255+192-$i ; fi
        let r="($v*$4+(255-$v)*$1)/255"
        let g="($v*$5+(255-$v)*$2)/255"
        let b="($v*$6+(255-$v)*$3)/255"
        setBackgroundColor $r $g $b " "
        if [ $i -eq 63 ] ; then resetColor $'\n' ; fi
        if [ $i -eq 127 ] ; then resetColor $'\n' ; fi
        if [ $i -eq 191 ] ; then resetColor $'\n' ; fi
    done
    resetColor $'\n'
}

colorRamp3() {
    for i in `seq 0 255`; do
        if [ $i -lt 128 ] ; then let v=i ; else let v=255+128-i ; fi
        let r="($v*$4+(255-$v)*$1)/255"
        let g="($v*$5+(255-$v)*$2)/255"
        let b="($v*$6+(255-$v)*$3)/255"
        setBackgroundColor $r $g $b "$7"
        if [ $i -eq 127 ] ; then resetColor $'\n' ; fi
    done
    resetColor $'\n'
}

rainbowRamp() {
    for i in `seq 0 255`; do
        if [ $i -lt 64 ] ; then let v=$i ;
        elif [ $i -lt 128 ] ; then let v=127+64-$i ;
        elif [ $i -lt 192 ] ; then let v=$i ;
        else let v=255+192-$i ; fi
        setBackgroundColor `rainbowColor $v` ' '
        if [ $i -eq 63 ] ; then resetColor $'\n' ; fi
        if [ $i -eq 127 ] ; then resetColor $'\n' ; fi
        if [ $i -eq 191 ] ; then resetColor $'\n' ; fi
    done
    resetColor $'\n'
}

colorCube4x4x4() {
  for i in `seq 0 3`; do
    for g in `seq 0 15`; do
      for r in `seq 0 3`; do
        for b in `seq 0 15`; do
          let r1=$r+$i*4
          let rr=$r1*17
          let gg=$g*17
          let bb=$b*17
          setBackgroundColor $rr $gg $bb "$1"
        done
        resetColor $' '
      done
      resetColor $'\n'
    done
    resetColor $'\n'
  done
}

xtermPalette() {
    output $'System colors:\n'
    if [ "$check" == "" ] ; then
        setBackgroundColor 0x00 0x00 0x00 "$1"
        setBackgroundColor 0xBB 0x00 0x00 "$1"
        setBackgroundColor 0x00 0xBB 0x00 "$1"
        setBackgroundColor 0xBB 0xBB 0x00 "$1"
        setBackgroundColor 0x00 0x00 0xBB "$1"
        setBackgroundColor 0xBB 0x00 0xBB "$1"
        setBackgroundColor 0x00 0xBB 0xBB "$1"
        setBackgroundColor 0xBB 0xBB 0xBB "$1"
        setBackgroundColor 0x55 0x55 0x55 "$1"
        setBackgroundColor 0xFF 0x55 0x55 "$1"
        setBackgroundColor 0x55 0xFF 0x55 "$1"
        setBackgroundColor 0xFF 0xFF 0x55 "$1"
        setBackgroundColor 0x55 0x55 0xFF "$1"
        setBackgroundColor 0xFF 0x55 0xFF "$1"
        setBackgroundColor 0x55 0xFF 0xFF "$1"
        setBackgroundColor 0xFF 0xFF 0xFF "$1"
        resetColor $'  iTerm2 Default (00 55 BB FF)\n'
    fi
    paletteRamp 0 16 "$1" $'  0..15 terminal colors\n'
    if [ "$check" == "" ] ; then
        setBackgroundColor 0x00 0x00 0x00 "$1"
        setBackgroundColor 0xc9 0x1b 0x00 "$1"
        setBackgroundColor 0x00 0xc2 0x00 "$1"
        setBackgroundColor 0xc7 0xc4 0x00 "$1"
        setBackgroundColor 0x02 0x25 0xc7 "$1"
        setBackgroundColor 0xc9 0x30 0xc7 "$1"
        setBackgroundColor 0x00 0xc5 0xc7 "$1"
        setBackgroundColor 0xc7 0xc7 0xc7 "$1"
        setBackgroundColor 0x67 0x67 0x67 "$1"
        setBackgroundColor 0xff 0x6d 0x67 "$1"
        setBackgroundColor 0x5f 0xf9 0x67 "$1"
        setBackgroundColor 0xfe 0xfb 0x67 "$1"
        setBackgroundColor 0x68 0x71 0xff "$1"
        setBackgroundColor 0xff 0x76 0xff "$1"
        setBackgroundColor 0x5f 0xfd 0xff "$1"
        setBackgroundColor 0xff 0xfe 0xff "$1"
        resetColor $'  iTerm2 Default colors (measured)\n'
    fi
    paletteRamp 0 16 "$1" $'  0..15 terminal colors\n'
    if [ "$check" == "" ] ; then
        setBackgroundColor 0x00 0x00 0x00 "$1" Black
        setBackgroundColor 0x80 0x00 0x00 "$1" Maroon
        setBackgroundColor 0x00 0x80 0x00 "$1" Green
        setBackgroundColor 0x80 0x80 0x00 "$1" Olive
        setBackgroundColor 0x00 0x00 0x80 "$1" Navy
        setBackgroundColor 0x80 0x00 0x80 "$1" Purple
        setBackgroundColor 0x00 0x80 0x80 "$1" Teal
        setBackgroundColor 0xc0 0xc0 0xc0 "$1" Silver
        setBackgroundColor 0x80 0x80 0x80 "$1" Grey
        setBackgroundColor 0xff 0x00 0x00 "$1" Red
        setBackgroundColor 0x00 0xff 0x00 "$1" Lime
        setBackgroundColor 0xff 0xff 0x00 "$1" Yellow
        setBackgroundColor 0x00 0x00 0xff "$1" Blue
        setBackgroundColor 0xff 0x00 0xff "$1" Fuchsia
        setBackgroundColor 0x00 0xff 0xff "$1" Aqua
        setBackgroundColor 0xff 0xff 0xff "$1" White
        resetColor $'  CGA palette\n'
    fi
    output $'Color cube, 6x6x6:\n'
    for jj in 0 18; do
      for ii in 0 1 2 3 4 5; do
        let start=16+$ii*36+$jj
        let startE=$ii*36+$jj
        paletteRamp $start 18 "$1" $'\n'
        if [ "$check" == "" ] ; then
            paletteEmulate $startE 18 "$1"
        fi
      done
    done
    output $'Grayscale ramp:\n'
    paletteRamp 232 24 "$1" $'  232..256\n'
    if [ "$check" == "" ] ; then
        for i in 0x08 0x12 0x1c 0x26 0x30 0x3a 0x44 0x4e \
                 0x58 0x62 0x6c 0x76 0x80 0x8a 0x94 0x9e \
                 0xa8 0xb2 0xbc 0xc6 0xd0 0xda 0xe4 0xee ; do
            setBackgroundColor $i $i $i "$1"
        done
        resetColor $'  8 18...228 238\n'
    fi
}

#output $'\n'
#output $'Color cube 4x4x4:\n';
#colorCube4x4x4 $'  '
#
#output $'\n'
#output $'Color ramps: fade to black\n';
#colorRamp1 1 0 0 $' '
#colorRamp1 0 1 0 $' '
#colorRamp1 0 0 1 $' '
#colorRamp1 0 1 1 $' '
#colorRamp1 1 0 1 $' '
#colorRamp1 1 1 0 $' '
#
## output $'Color edges\n';
## colorRamp3 255   0   0   255   0 255 $' '
## colorRamp3   0 255   0   255 255   0 $' '
## colorRamp3   0   0 255     0 255 255 $' '
#
#output $'\n'
#output $'Color ramps: fade to white\n';
#colorRamp3 255   0   0  255 255 255 $' '
#colorRamp3   0 255   0  255 255 255 $' '
#colorRamp3   0   0 255  255 255 255 $' '
#colorRamp3   0 255 255  255 255 255 $' '
#colorRamp3 255   0 255  255 255 255 $' '
#colorRamp3 255 255   0  255 255 255 $' '
#
#output $'\n'
#output $'Grayscale ramp:\n';
#colorRamp3   0   0   0  255 255 255 $' '
#
#output $'\n'
#output $'Rainbow ramp:\n';
#rainbowRamp $' '

output $'\n'
output $'xterm palette:\n';
xtermPalette $'   '

flush;
