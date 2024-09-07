#!/bin/bash

echo "256 color palette:" > $1
for i in `seq 0 9`; do printf "\033]4;$i;?\007"; read -n 25 pal; echo $pal >> $1; printf "\r"; done
for i in `seq 10 99`; do printf "\033]4;$i;?\007"; read -n 26 pal; echo $pal >> $1; printf "\r"; done
for i in `seq 100 255`; do printf "\033]4;$i;?\007"; read -n 27 pal; echo $pal >> $1; printf "\r"; done
echo "special colors as palette entries:" >> $1
# Pc = 0  ⇐  resource colorBD (BOLD).
# Pc = 1  ⇐  resource colorUL (UNDERLINE).
# Pc = 2  ⇐  resource colorBL (BLINK).
# Pc = 3  ⇐  resource colorRV (REVERSE).
# Pc = 4  ⇐  resource colorIT (ITALIC).
for i in `seq 256 260`; do printf "\033]4;$i;?\007"; read -n 27 pal; echo $pal >> $1; printf "\r"; done
# not supported by iTerm2
#echo "special colors:" >> $1
#for i in `seq 0 4`; do printf "\033]5;$i;?\007"; read -n 25 pal; echo $pal >> $1; printf "\r"; done
echo "dynamic colors:" >> $1
# text default foreground and background colors
for i in `seq 10 11`; do printf "\033]$i;?\007"; read -n 24 pal; echo $pal >> $1; printf "\r"; done
# other dynamic colors (not supported by iTerm2)
#for i in `seq 10 19`; do printf "\033]$i;?\007"; read -n 24 pal; echo $pal >> $1; printf "\r"; done
printf "\033[K"
