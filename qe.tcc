#!/bin/sh
#
# QEmacs can be compiled directly with TinyCC. Launch this script to
# compile and execute qemacs with TinyCC.
#

# just for benchmarking
tcc_opts=""
libs="-L/usr/X11R6/lib -lXext -lXv -lX11"
#tcc="tcc"
tcc="../tcc/tcc"
if [ "$1" = "-bench" ] ; then
  tcc_opts="$tcc_opts -o /tmp/qe -bench"
  shift
fi
if [ "$1" = "-gprof" ] ; then
  tcc="${tcc}_p"
  libs=""
  shift
fi
if [ "$1" = "-g" ] ; then
  tcc_opts="$tcc_opts -g"
  shift
fi
if [ "$1" = "-b" ] ; then
  tcc_opts="$tcc_opts -b"
  shift
fi

$tcc $tcc_opts $libs -DHAVE_QE_CONFIG_H -DQE_VERSION=\"0.3tcc\" -Iliburlio -- \
           qe.c charset.c buffer.c input.c unicode_join.c \
           qfribidi.c \
           display.c tty.c util.c hex.c list.c \
           clang.c shell.c \
           css.c cssparse.c xmlparse.c html.c html_style.c \
           x11.c \
           liburlio/cutils.c liburlio/urlmisc.c liburlio/mem.c \
           liburlio/urlio.c liburlio/file.c liburlio/dns.c \
           liburlio/tcp.c liburlio/http.c \
           qeend.c -- $*
