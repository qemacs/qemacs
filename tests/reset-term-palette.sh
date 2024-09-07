#!/usr/bin/env bash
#
# this script resets the terminal colors to its default settings
#
# reset all palette colors
printf "\033]104\007"
# reset all special colors
printf "\033]105\007"
# reset default text foreground color
printf "\033]110\007"
# reset default text background color
printf "\033]111\007"
