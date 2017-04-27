#!/usr/bin/env ksh
#!/bin/bash

# Charles Cooke's 16-color Mandelbrot
# http://earth.gkhs.net/ccooke/shell.html
# Combined Bash/ksh93 flavors by Dan Douglas (ormaaj)
# Charlie Gordon 2017: Fix aspect ratio, use half block to double vertical resolution

declare lastbg=x lastfg=x

function doBash {
    declare -i i j nx ny cols=$1 rows=$2 cb=0 m=16
    declare -i P=10**8 Q=P/10 bb=4*P**2 xc=-4*Q yc=0 dx=32*Q/cols dy=12*dx/10 x y a b c
    for ((y=yc-dy*rows,ny=0; ny++<rows; y+=dy+dy)); do
      for ((x=xc-dx*cols/2,nx=0; nx++<cols; x+=dx)); do
        for ((a=b=i=0; a**2+b**2<=bb && i++<99; c=a,a=(a**2-b**2)/P+x,b=2*c*b/P+y)); do :
        done
        for ((a=b=j=0; a**2+b**2<=bb && j++<99; c=a,a=(a**2-b**2)/P+x,b=2*c*b/P+y+dy)); do :
        done
        colorBox $((i<99?i%m+cb:cb)) $((j<99?j%m+cb:cb))
      done
      resetColor
    done
}

function doKsh {
    integer i j nx ny cols=$1 rows=$2 cb=0 m=16
    float xc=-0.4 yc=0 dx=3.2/cols dy=1.2*dx x y a b c
    for ((y=yc-dy*rows,ny=0; ny++<rows; y+=dy+dy)); do :
      for ((x=xc-dx*cols/2,nx=0; nx++<cols; x+=dx)); do :
        for ((a=b=i=0; a**2+b**2<=4 && i++<99; c=a,a=a**2-b**2+x,b=2*c*b+y)); do :
        done
        for ((a=b=j=0; a**2+b**2<=4 && j++<99; c=a,a=a**2-b**2+x,b=2*c*b+y+dy)); do :
        done
        . colorBox $((i<99?i%m+cb:cb)) $((j<99?j%m+cb:cb))
      done
      resetColor
    done
}

function resetColor {
    lastbg=x
    lastfg=x
    echo -e "\033[m"
}

function colorBox {
    # \u2584 (in utf-8 \xE2\x96\x84 is the top square
    #(($1==lastclr)) || printf %s "${colrs[lastclr=$1]:=$(tput setab "$1")}"
    #printf "\033[48;5;${1}m"
    #printf "\033[38;5;${2}m"
    #printf '\u2584'
    if [ $1 != $lastbg ]; then
        printf "\033[48;5;$((lastbg=$1))m"
    fi
    if [ $1 == $2 ]; then
        printf " "
    else
        if [ $2 != $lastfg ]; then
            printf "\033[38;5;$((lastfg=$2))m"
        fi
        printf "\342\226\204"
    fi
}

#unset -v lastclr
#typeset -a colrs
trap 'tput sgr0' EXIT
${KSH_VERSION+. doKsh} ${BASH_VERSION+doBash} $(($(tput cols)-2)) $(($(tput lines)-1))
