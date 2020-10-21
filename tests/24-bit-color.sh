#!/bin/bash
# This file was originally taken from iterm2
# https://github.com/gnachman/iTerm2/blob/master/tests/24-bit-color.sh
#
#   This file echoes a bunch of 24-bit color codes
#   to the terminal to demonstrate its functionality.
#   The foreground escape sequence is ^[38;2;<r>;<g>;<b>m
#   The background escape sequence is ^[48;2;<r>;<g>;<b>m
#   <r> <g> <b> range from 0 to 255 inclusive.
#   The escape sequence ^[0m returns output to default

setBackgroundColor() {
    echo -ne "\033[48;2;$1;$2;$3m$4"
}

resetColor() {
    echo -ne "\033[0m\n"
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

    if   [ $h -eq 0 ] ; then echo "255 $t 0" ;
    elif [ $h -eq 1 ] ; then echo "$q 255 0" ;
    elif [ $h -eq 2 ] ; then echo "0 255 $t" ;
    elif [ $h -eq 3 ] ; then echo "0 $q 255" ;
    elif [ $h -eq 4 ] ; then echo "$t 0 255" ;
    elif [ $h -eq 5 ] ; then echo "255 0 $q" ;
    else                     echo "0 0 0"    ; # never reached
    fi
}

colorRamp() {
    for i in `seq 0 255`; do
        if   [ $i -lt  64 ] ; then let v=$i ;
        elif [ $i -lt 128 ] ; then let v=192-$i ;
        elif [ $i -lt 192 ] ; then let v=$i ;
        else                       let v=255+192-$i ;
        fi
        let r=$v*$1
        let g=$v*$2
        let b=$v*$3
        setBackgroundColor $r $g $b $' '
        if [ $i -eq  63 ] ; then resetColor ; fi
        if [ $i -eq 127 ] ; then resetColor ; fi
        if [ $i -eq 191 ] ; then resetColor ; fi
    done
    resetColor
}

colorRamp2() {
    for i in `seq 0 255`; do
        if   [ $i -lt  64 ] ; then let v=$i ;
        elif [ $i -lt 128 ] ; then let v=192-$i ;
        elif [ $i -lt 192 ] ; then let v=$i ;
        else                       let v=255+192-$i ;
        fi
        let r="($v*$4+(255-$v)*$1)/255"
        let g="($v*$5+(255-$v)*$2)/255"
        let b="($v*$6+(255-$v)*$3)/255"
        setBackgroundColor $r $g $b $' '
        if [ $i -eq  63 ] ; then resetColor ; fi
        if [ $i -eq 127 ] ; then resetColor ; fi
        if [ $i -eq 191 ] ; then resetColor ; fi
    done
    resetColor
}

rainbowRamp() {
    for i in `seq 0 255`; do
        if   [ $i -lt  64 ] ; then let v=$i ;
        elif [ $i -lt 128 ] ; then let v=127+64-$i ;
        elif [ $i -lt 192 ] ; then let v=$i ;
        else                       let v=255+192-$i ;
        fi
        setBackgroundColor `rainbowColor $v` $' '
        if [ $i -eq  63 ] ; then resetColor ; fi
        if [ $i -eq 127 ] ; then resetColor ; fi
        if [ $i -eq 191 ] ; then resetColor ; fi
    done
    resetColor
}

colorRamp 1 0 0
colorRamp 0 1 0
colorRamp 0 0 1
colorRamp 0 1 1
colorRamp 1 0 1
colorRamp 1 1 0
colorRamp 1 1 1

rainbowRamp 1 1 1
