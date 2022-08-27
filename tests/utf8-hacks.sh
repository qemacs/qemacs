#!/bin/bash
printf '\xE4\xB8\xAD\xE6\x96\x87\t\t\xE4\xB8\xAD\xE6\x96\x87\n'
printf '\xE4\xB8\xAD\xE6\x96\x87\bx\t\t\xE4\xB8\xAD x\n'
printf '\xE4\xB8\xAD\xE6\x96\x87\b\bx\t\t\xE4\xB8\xADx \n'
printf '\xE4\xB8\xAD\xE6\x96\x87\tx\b\by\t\xE4\xB8\xAD\xE6\x96\x87   yx\n'
printf 'Repeat 1+9 \xE4\xB8\xAD\e[9b|\n'
printf 'Repeat 10 after space \xE4\xB8\xAD \e[10b|\n'

printf "x <- left%91s\n" "x at right -> x"
printf "x <- left%91s\by\n" "y at right -> x"
printf "x <- left%91sx\b\by\n\n" "y at right, x next row -> x"
printf "x <- left%78sright -> \xE4\xB8\xAD\xE6\x96\x87\n" ""
printf "x <- left%79sright -> \xE4\xB8\xAD\xE6\x96\x87\n" ""

printf "%-100sxx\ry\t\tyx\n" "long line with xx wrapping to next line \r y should show yx"
printf "\t1\t2\t3\t4\t5\t6\t7\t8\t9\t0\ta\tb\tc\td\te\n"
printf "\t\b1\t\b2\t\b3\t\b4\t\b5\t\b6\t\b7\t\b8\t\b9\t\b0\t\ba\t\bb\t\bc\t\bd\t\be\n"
printf "\e[I1\e[I2\e[I3\e[I4\e[I5\e[I6\e[I7\e[I8\e[I9\e[I0\e[Ia\e[Ib\e[Ic\e[Id\e[Ie\n"
printf "\e[2I1\e[2I2\e[2I3\e[2I4\e[2I5\e[2I6\e[2I7\e[2I8\e[2I9\e[2I0\e[2Ia\e[2Ib\e[2Ic\e[2Id\e[2Ie\n"
printf "\e[12Ix\n"
printf "\e[13Ix\n"
printf "\e[14Ix\n"
printf "\e[15Ix\n"
printf "\e[12Ix\ty\n"
printf "\e[13Ix\ty\n"
printf "\e[14Ix\t\ty\n"
printf "\e[15Ix\t\t\ty\n"
# testing backtab
#printf "\t1\t2\t3\t4\t5\t6\t7\t8\t9\t0\e[Z1\e[Z2\e[Z3\e[Z4\e[Z5\e[Z6\e[Z7\e[Z8\e[Z9\e[Z0\n"
# testing raw output
#printf "\e[3h\001\002\003\004\005\006\007\010\e[3l\n"
