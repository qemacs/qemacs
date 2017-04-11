#!/bin/bash
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
    output "\033[48;2;$1;$2;$3m$4"
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

output $'\n'
output $'Color cube 4x4x4:\n';
for i in `seq 0 3`; do
  for g in `seq 0 15`; do
    for r in `seq 0 3`; do
      for b in `seq 0 15`; do
        let r1=$r+$i*4
        let rr=$r1*17
        let gg=$g*17
        let bb=$b*17
        setBackgroundColor $rr $gg $bb $'  '
      done
      resetColor $' '
    done
    resetColor $'\n'
  done
  resetColor $'\n'
done

output $'\n'
output $'Color ramps: fade to black\n';
colorRamp1 1 0 0 $' '
colorRamp1 0 1 0 $' '
colorRamp1 0 0 1 $' '
colorRamp1 0 1 1 $' '
colorRamp1 1 0 1 $' '
colorRamp1 1 1 0 $' '

# output $'Color edges\n';
# colorRamp3 255   0   0   255   0 255 $' '
# colorRamp3   0 255   0   255 255   0 $' '
# colorRamp3   0   0 255     0 255 255 $' '

output $'\n'
output $'Color ramps: fade to white\n';
colorRamp3 255   0   0  255 255 255 $' '
colorRamp3   0 255   0  255 255 255 $' '
colorRamp3   0   0 255  255 255 255 $' '
colorRamp3   0 255 255  255 255 255 $' '
colorRamp3 255   0 255  255 255 255 $' '
colorRamp3 255 255   0  255 255 255 $' '

output $'\n'
output $'Grayscale ramp:\n';
colorRamp3   0   0   0  255 255 255 $' '

output $'\n'
output $'Rainbow ramp:\n';
rainbowRamp $' '

output $'\n'
output $'xterm palette:\n';
{
    for i in `seq 0 127`; do
        output "\033[48;5;${i}m "
    done
    resetColor $'\n'
    for i in `seq 128 255`; do
        output "\033[48;5;${i}m "
    done
    resetColor $'\n'
}
flush;
