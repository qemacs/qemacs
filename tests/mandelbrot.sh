#!/usr/bin/env ksh

# Charles Cooke's 16-color Mandelbrot
# http://earth.gkhs.net/ccooke/shell.html
# Combined Bash/ksh93 flavors by Dan Douglas (ormaaj)

function doBash {
    typeset P Q X Y a b c i j v x y 
    for ((P=10**8,Q=P/100,X=320*Q/cols,Y=210*Q/lines,y=-105*Q,y1=y+Y/2,v=-220*Q,x=v; y<105*Q; x=v,y+=Y,y1+=Y)); do
        for ((;x<P;a=b=i=j=c=0,x+=X)); do
            for ((; a**2+b**2<4*P**2&&i++<99; a=((c=a)**2-b**2)/P+x,b=2*c*b/P+y)); do :
            done
            for ((; a**2+b**2<4*P**2&&j++<99; a=((c=a)**2-b**2)/P+x,b=2*c*b/P+y1)); do :
            done
            colorBox $((i<99?i%16:0)) $((j<99?j%16:0))
        done
        echo
    done
}

function doKsh {
    integer i j
    float a b c x=2.2 y=-1.05 y2=y+Y/2 X=3.2/cols Y=2.1/lines 
    while
        for ((a=b=i=0;(c=a)**2+b**2<=2&&i++<99&&(a=a**2-b**2+x,b=2*c*b+y);)); do :
        done
        for ((a=b=j=0;(c=a)**2+b**2<=2&&j++<99&&(a=a**2-b**2+x,b=2*c*b+y2);)); do :
        done
        . colorBox $((i<99?i%16:0)) $((j<99?j%16:0))
        if ((x<1?!(x+=X):(y+=Y,y2+=Y,x=-2.2))); then
            print
            ((y<1.05)) 
        fi
        do :
    done
}

function colorBox {
    printf "\033[48;5;$1m"
    printf "\033[38;5;$2m"
    #(($1==lastclr)) || printf %s "${colrs[lastclr=$1]:=$(tput setab "$1")}"
    #(($2==lastclr2)) || printf %s "${colrs[lastclr2=$2]:=$(tput setaf "$2")}"
    printf '\u2584'
}

unset -v lastclr
((cols=$(tput cols)-2, lines=$(tput lines)))
typeset -a colrs
trap 'tput sgr0; echo' EXIT
${KSH_VERSION+. doKsh} ${BASH_VERSION+doBash}
