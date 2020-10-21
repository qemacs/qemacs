#!/bin/bash

# '<' '%%' '>' are VT220 only
for x in '0' ; do
  if [ "$1" == "--ascii" ] ; then
    # ASCII version
    printf "+-----------------------------------------------------------------------------+\n"
    printf "|  VT100 line drawing character set table using ^[(B^[)$x ^N and ^O sequences  |\n"
    printf "+-----------------------------------------------------------------------------+\n"
    for i in {0..15} ; do
      for j in 32 48 64 80 96 112 ; do
        n=$[$i+$j]
        c=$(printf "\\%03o" $n)
        printf "| %02X | $c | \016$c\017 " $[i+j]
      done
      printf "|\n"
    done
    printf "+-----------------------------------------------------------------------------+\n\n"
  else
    # Using VT100 line drawing characters
    printf "\033(B\033)$x"
    printf "\016lqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqk\017\n"
    printf "\016x\017  VT100 line drawing character set table using ^[(B^[)$x ^N and ^O sequences  \016x\017\n"
    printf "\016tqqqqwqqqwqqqwqqqqwqqqwqqqwqqqqwqqqwqqqwqqqqwqqqwqqqwqqqqwqqqwqqqwqqqqwqqqwqqqu\017\n"
    for i in {0..15} ; do
      for j in 32 48 64 80 96 112 ; do
        n=$[$i+$j]
        c=$(printf "\\%03o" $n)
        printf "\016x\017 %02X \016x\017 $c \016x\017 \016$c\017 " $n
      done
      printf "\016x\017\n"
    done
    printf "\016mqqqqvqqqvqqqvqqqqvqqqvqqqvqqqqvqqqvqqqvqqqqvqqqvqqqvqqqqvqqqvqqqvqqqqvqqqvqqqj\017\n"
  fi
done
