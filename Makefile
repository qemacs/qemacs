# QEmacs, tiny but powerful multimode editor
#
# Copyright (c) 2000-2002 Fabrice Bellard.
# Copyright (c) 2000-2021 Charlie Gordon.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

DEPTH=.

include $(DEPTH)/config.mak

ifeq (,$(V)$(VERBOSE))
    echo := @echo
    cmd  := @
else
    echo := @:
    cmd  :=
endif

ifeq ($(CC),gcc)
  CFLAGS  += -g -O2 -funsigned-char -Wall
  # do not warn about zero-length formats.
  CFLAGS  += -Wno-format-zero-length
  LDFLAGS += -g
endif

#include local compiler configuration file
-include $(DEPTH)/cflags.mk

ifdef CONFIG_DARWIN
  CFLAGS += -Wno-string-plus-int
else
  CFLAGS += -Wno-unused-result
endif

ifdef TARGET_GPROF
  CFLAGS  += -p
  LDFLAGS += -p
endif

CFLAGS+=-I$(DEPTH)

ifdef TARGET_ARCH_X86
  #CFLAGS+=-fomit-frame-pointer
  ifeq ($(GCC_MAJOR),2)
    CFLAGS += -m386 -malign-functions=0
  else
    CFLAGS += -march=i386 -falign-functions=0
  endif
endif

HOST_CFLAGS:=$(CFLAGS)

DEFINES=-DHAVE_QE_CONFIG_H

########################################################
# do not modify after this

ifdef DEBUG
DEBUG_SUFFIX:=_debug
ECHO_CFLAGS += -DCONFIG_DEBUG
CFLAGS += -g -O0
LDFLAGS += -g -O0
else
DEBUG_SUFFIX:=
endif

TARGETLIBS:=

ifeq (,$(TARGET))
TARGET:=qe
TARGETS:=kmaps ligatures tqe
TOP:=1
else
TOP:=0
endif

OBJS:= qe.o util.o cutils.o charset.o buffer.o search.o input.o display.o \
       modes/hex.o modes/list.o

ifdef TARGET_TINY
ECHO_CFLAGS += -DCONFIG_TINY
#CFLAGS += -DCONFIG_TINY -m32 -Os
CFLAGS += -DCONFIG_TINY -Os
OBJS+= parser.o
else
OBJS+= qescript.o extras.o variables.o
endif

ifdef CONFIG_DARWIN
  LDFLAGS += -L/opt/local/lib/
endif

ifdef CONFIG_PNG_OUTPUT
  HTMLTOPPM_LIBS += -lpng
endif

ifdef CONFIG_DLL
  LIBS += $(DLLIBS)
  # export some qemacs symbols
  LDFLAGS += -Wl,-E
endif

ifdef CONFIG_DOC
  TARGETS += qe-doc.html
endif

ifdef CONFIG_HAIKU
  OBJS += haiku.o
  LIBS += -lbe -lstdc++
endif

ifdef CONFIG_WIN32
  OBJS+= unix.o win32.o
#  OBJS+= printf.o
  LIBS+= -lmsvcrt -lgdi32 -lwsock32
else
  OBJS+= unix.o tty.o
  LIBS+= $(EXTRALIBS)
endif

ifndef TARGET_TINY
ifdef CONFIG_QSCRIPT
  OBJS+= qscript.o eval.o
endif

ifdef CONFIG_ALL_KMAPS
  OBJS+= kmap.o
endif

ifdef CONFIG_UNICODE_JOIN
  OBJS+= unicode_join.o arabic.o indic.o qfribidi.o
endif

# more charsets if needed
OBJS+= charsetjis.o charsetmore.o

ifdef CONFIG_ALL_MODES
OBJS+= modes/unihex.o   modes/bufed.o    modes/orgmode.o  modes/markdown.o \
       lang/clang.o     lang/xml.o       lang/htmlsrc.o   lang/forth.o     \
       lang/arm.o       lang/lisp.o      lang/makemode.o  lang/perl.o      \
       lang/script.o    lang/ebnf.o      lang/cobol.o     lang/rlang.o     \
       lang/txl.o       lang/nim.o       lang/rebol.o     lang/elm.o       \
       lang/jai.o       lang/ats.o       lang/rust.o      lang/swift.o     \
       lang/icon.o      lang/groovy.o    lang/virgil.o    lang/ada.o       \
       lang/basic.o     lang/vimscript.o lang/pascal.o    lang/fortran.o   \
       lang/haskell.o   lang/lua.o       lang/python.o    lang/ruby.o      \
       lang/smalltalk.o lang/sql.o       lang/elixir.o    lang/agena.o     \
       lang/coffee.o    lang/erlang.o    lang/julia.o     lang/ocaml.o     \
       lang/scad.o      lang/magpie.o    lang/falcon.o    lang/wolfram.o   \
       lang/tiger.o     lang/asm.o       lang/inifile.o   lang/postscript.o \
       modes/fractal.o  lang/extra-modes.o $(EXTRA_MODES)
ifndef CONFIG_WIN32
OBJS+= modes/shell.o    modes/dired.o    modes/archive.o  modes/latex-mode.o
endif
endif

# currently not used in qemacs
ifdef CONFIG_CFB
  OBJS+= libfbf.o fbfrender.o cfb.o fbffonts.o
endif

ifdef CONFIG_HTML
  LIBQHTML:= $(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/libqhtml$(DEBUG_SUFFIX).a
  QHTML_LIBS:= -L$(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/ -lqhtml$(DEBUG_SUFFIX)
  CFLAGS+= -I./libqhtml
  HOST_CFLAGS+= -I./libqhtml
  DEP_LIBS+= $(LIBQHTML)
  LIBS+= $(QHTML_LIBS)
  OBJS+= modes/html.o modes/docbook.o
  ifndef CONFIG_WIN32
    TARGETLIBS+= libqhtml
    TARGETS+= html2png$(EXE)
  endif
endif

ifdef CONFIG_FFMPEG
  OBJS+= modes/video.o modes/image.o
  DEP_LIBS+= $(FFMPEG_LIBDIR)/libavcodec/libavcodec.a $(FFMPEG_LIBDIR)/libavformat/libavformat.a
  LIBS+= -L$(FFMPEG_LIBDIR)/libavcodec -L$(FFMPEG_LIBDIR)/libavformat -lavformat -lavcodec -lz -lpthread
  DEFINES+= -I$(FFMPEG_SRCDIR)/libavcodec -I$(FFMPEG_SRCDIR)/libavformat
  TARGETS+= ffplay$(EXE)
else
  OBJS+= modes/stb.o
endif

ifdef CONFIG_X11
  TARGETS += xqe
endif

ifdef TARGET_X11
  OBJS+= x11.o
  ECHO_CFLAGS += -DCONFIG_X11
  DEFINES += -DCONFIG_X11
  CFLAGS += $(XCFLAGS)
  LDFLAGS += $(XLDFLAGS)
  LIBS += $(XLIBS)
  ifdef CONFIG_XRENDER
    LIBS += -lXrender
  endif
  ifdef CONFIG_XV
    LIBS += -lXv
  endif
  ifdef CONFIG_XSHM
    LIBS += -lXext
  endif
  LIBS += -lX11 $(DLLIBS)
endif
endif

ifdef CONFIG_INIT_CALLS
  # must be the last object
  OBJS+= qeend.o
endif

SRCS:= $(OBJS:.o=.c)

DEPENDS:= qe.h config.h cutils.h display.h qestyles.h variables.h config.mak lang/clang.h
DEPENDS:= $(addprefix $(DEPTH)/, $(DEPENDS))

BINDIR:=$(DEPTH)/bin

OBJS_DIR:= $(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/$(TARGET)$(DEBUG_SUFFIX)
CFLAGS+= -I$(OBJS_DIR)
OBJS:= $(addprefix $(OBJS_DIR)/, $(OBJS))

$(shell mkdir -p $(OBJS_DIR) $(BINDIR))

#
# Dependencies
#
ifeq (1,$(TOP))
all: $(TARGETLIBS) $(TARGET)$(DEBUG_SUFFIX)$(EXE) $(TARGETS)
else
all: $(TARGETLIBS) $(TARGET)$(DEBUG_SUFFIX)$(EXE)
endif

libqhtml: force
	$(MAKE) -C libqhtml all

ifdef DEBUG
$(TARGET)$(DEBUG_SUFFIX)$(EXE): $(OBJS) $(DEP_LIBS)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
else
$(TARGET)_g$(EXE): $(OBJS) $(DEP_LIBS)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(TARGET)$(EXE): $(TARGET)_g$(EXE) Makefile
	@rm -f $@
	cp $< $@
	-$(STRIP) $@
	@ls -l $@
	@echo `$(SIZE) $@` `wc -c $@` $(TARGET) $(OPTIONS) \
		| cut -d ' ' -f 7-10,13,15-40 >> STATS
endif

ifeq (1,$(TOP))

# targets that require recursion
xqe:       force;	$(MAKE) TARGET=xqe TARGET_X11=1
tqe:       force;	$(MAKE) TARGET=tqe TARGET_TINY=1
debug:     force;	$(MAKE) TARGET=qe DEBUG=1
qe_debug:  force;	$(MAKE) TARGET=qe DEBUG=1
xqe_debug: force;	$(MAKE) TARGET=xqe TARGET_X11=1 DEBUG=1
tqe_debug: force;	$(MAKE) TARGET=tqe TARGET_TINY=1 DEBUG=1
tqe1:      force;	$(MAKE) TARGET=tqe TARGET_TINY=1 tqe1$(EXE)

else

# Amalgation mode produces a larger executable
TSRCS:=qe.c util.c cutils.c charset.c buffer.c search.c input.c display.c \
       modes/hex.c modes/list.c parser.c unix.c tty.c win32.c qeend.c

tqe1_g$(EXE): tqe.c $(TSRCS) Makefile $(OBJS_DIR)/modules.txt
	$(echo) CC $(ECHO_CFLAGS) -o $@ $<
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBS)

tqe1$(EXE): tqe1_g$(EXE) Makefile
	@rm -f $@
	cp $< $@
	-$(STRIP) $@
	@ls -l $@
	@echo `$(SIZE) $@` `wc -c $@` tqe1 $(OPTIONS) \
		| cut -d ' ' -f 7-10,13,15-40 >> STATS
endif

ifdef CONFIG_FFMPEG
ffplay$(EXE): qe$(EXE) Makefile
	ln -sf $< $@
endif

ifndef CONFIG_INIT_CALLS
$(OBJS_DIR)/qe.o: $(OBJS_DIR)/modules.txt
endif

$(OBJS_DIR)/modules.txt: $(SRCS) Makefile
	@echo creating $@
	@echo '/* This file was generated automatically */' > $@
	@grep -h ^qe_module_init $(SRCS) | \
            sed s/qe_module_init/qe_module_declare/ >> $@

$(OBJS_DIR)/cfb.o: cfb.c cfb.h fbfrender.h
$(OBJS_DIR)/charset.o: charset.c unicode_width.h
$(OBJS_DIR)/charsetjis.o: charsetjis.c charsetjis.def
$(OBJS_DIR)/fbfrender.o: fbfrender.c fbfrender.h libfbf.h
$(OBJS_DIR)/qe.o: qe.c qeconfig.h qfribidi.h variables.h
$(OBJS_DIR)/qfribidi.o: qfribidi.c qfribidi.h
$(OBJS_DIR)/modes/stb.o: modes/stb.c modes/stb_image.h

$(OBJS_DIR)/%.o: %.c $(DEPENDS) Makefile
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

$(OBJS_DIR)/haiku.o: haiku.cpp $(DEPENDS) Makefile
	$(echo) CPP $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  g++ $(DEFINES) $(CFLAGS) -Wno-multichar -o $@ -c $<

# debugging targets
%.s: %.c $(DEPENDS) Makefile
	$(CC) $(DEFINES) $(CFLAGS) -o $@ -S $<

%.s: %.cpp $(DEPENDS) Makefile
	g++ $(DEFINES) $(CFLAGS) -Wno-multichar -o $@ -S $<

#
# Host utilities
#
$(BINDIR)/%$(EXE): tools/%.c
	$(echo) CC -o $@ $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

#
# Test for bidir algorithm
#
$(BINDIR)/qfribidi$(EXE): qfribidi.c cutils.c
	$(echo) CC -o $@ $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -DTEST -o $@ $^

#
# build ligature table
#
$(BINDIR)/ligtoqe$(EXE): tools/ligtoqe.c cutils.c
	$(echo) CC -o $@ $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

ifdef BUILD_ALL
ligatures: $(BINDIR)/ligtoqe$(EXE) unifont.lig
	$(BINDIR)/ligtoqe unifont.lig $@
endif

#
# Key maps build (Only useful if you want to build your own maps from yudit maps)
#
KMAPS=Arabic.kmap ArmenianEast.kmap ArmenianWest.kmap Chinese-CJ.kmap       \
      Cyrillic.kmap Czech.kmap DE-RU.kmap Danish.kmap Dutch.kmap            \
      Esperanto.kmap Ethiopic.kmap French.kmap Georgian.kmap German.kmap    \
      Greek.kmap GreekMono.kmap Guarani.kmap HebrewP.kmap Hungarian.kmap    \
      KOI8_R.kmap Lithuanian.kmap Mnemonic.kmap Polish.kmap Russian.kmap    \
      SGML.kmap TeX.kmap Troff.kmap VNtelex.kmap                            \
      Vietnamese.kmap XKB_iso8859-4.kmap                                    \
      DanishAlternate.kmap GreekBible.kmap Polytonic.kmap Spanish.kmap      \
      Thai.kmap VietnameseTelex.kmap Welsh.kmap                             \
      Hebrew.kmap HebrewIsraeli.kmap HebrewP.kmap Israeli.kmap Yiddish.kmap \
      Kana.kmap
#     Hangul.kmap Hangul2.kmap Hangul3.kmap Unicode2.kmap
#KMAPS_DIR=$(datadir)/yudit/data
KMAPS_DIR=kmap
KMAPS:=$(addprefix $(KMAPS_DIR)/, $(KMAPS))

$(BINDIR)/kmaptoqe$(EXE): tools/kmaptoqe.c cutils.c
	$(echo) CC -o $@ $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

ifdef BUILD_ALL
kmaps: $(BINDIR)/kmaptoqe$(EXE) $(KMAPS) Makefile
	$(BINDIR)/kmaptoqe $@ $(KMAPS)
endif

#
# Code pages (only useful to add your own code pages)
#
CP=  8859-2.TXT   8859-3.TXT   8859-4.TXT   8859-5.TXT   8859-6.TXT  \
     8859-7.TXT   8859-8.TXT   8859-9.TXT   8859-10.TXT  8859-11.TXT \
     8859-13.TXT  8859-14.TXT  8859-15.TXT  8859-16.TXT              \
     CP437.TXT    CP737.TXT    CP850.TXT    CP852.TXT    CP866.TXT   \
     CP1125.TXT   CP1250.TXT   CP1251.TXT   CP1252.TXT   CP1256.TXT  \
     CP1257.TXT   MAC-LATIN2.TXT MAC-ROMAN.TXT                       \
     kamen.cp     KOI8-R.TXT   KOI8-U.TXT   TCVN.TXT     VISCII.TXT  \
     CP037.TXT    CP424.TXT    CP500.TXT    CP875.TXT    CP1026.TXT  \
     ATARIST.TXT

CP:=$(addprefix cp/,$(CP))

JIS= JIS0208.TXT JIS0212.TXT
JIS:=$(addprefix cp/,$(JIS))

$(BINDIR)/cptoqe$(EXE): tools/cptoqe.c cutils.c
	$(echo) CC $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

$(BINDIR)/jistoqe$(EXE): tools/jistoqe.c cutils.c
	$(echo) CC $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

ifdef BUILD_ALL
charsetmore.c: cp/cpdata.txt $(CP) $(BINDIR)/cptoqe$(EXE) Makefile
	$(BINDIR)/cptoqe -i cp/cpdata.txt $(CP) > $@

charsetjis.def: $(JIS) $(BINDIR)/jistoqe$(EXE) Makefile
	$(BINDIR)/jistoqe $(JIS) > $@
endif

#
# fonts (only needed for html2png)
#
FONTS=fixed10.fbf fixed12.fbf fixed13.fbf fixed14.fbf \
      helv8.fbf helv10.fbf helv12.fbf helv14.fbf helv18.fbf helv24.fbf \
      times8.fbf times10.fbf times12.fbf times14.fbf times18.fbf times24.fbf \
      unifont.fbf
FONTS:=$(addprefix fonts/,$(FONTS))

$(BINDIR)/fbftoqe$(EXE): tools/fbftoqe.c cutils.c
	$(echo) CC -o $@ $^
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

fbffonts.c: $(BINDIR)/fbftoqe$(EXE) $(FONTS)
	$(BINDIR)/fbftoqe $(FONTS) > $@

#
# html2png tool (XML/HTML/CSS2 renderer test tool)
#
OBJS1=html2png.o util.o cutils.o \
      arabic.o indic.o qfribidi.o display.o unicode_join.o \
      charset.o charsetmore.o charsetjis.o \
      libfbf.o fbfrender.o cfb.o fbffonts.o

OBJS1:=$(addprefix $(OBJS_DIR)/, $(OBJS1))

html2png$(EXE): $(OBJS1) $(LIBQHTML)
	$(echo) LD $@
	$(cmd)  $(CC) $(LDFLAGS) -o $@ $(OBJS1) $(QHTML_LIBS) $(HTMLTOPPM_LIBS)

# autotest target
test:
	$(MAKE) -C tests test

# documentation
qe-doc.html: qe-doc.texi Makefile
	LANGUAGE=en_US LC_ALL=en_US.UTF-8 texi2html -monolithic $<
	@mv $@ $@.tmp
	@sed "s/This document was generated on.*//"     < $@.tmp | \
		sed "s/<!-- Created on .* by/<!-- Created by/" > $@
	@rm $@.tmp

#
# Maintenance targets
#
clean:
	$(MAKE) -C libqhtml clean
	rm -rf *.dSYM .objs* .tobjs* .xobjs* bin
	rm -f *~ *.o *.a *.exe *_g *_debug TAGS gmon.out core *.exe.stackdump \
           qe tqe tqe1 xqe qfribidi kmaptoqe ligtoqe html2png cptoqe jistoqe \
           fbftoqe fbffonts.c allmodules.txt basemodules.txt '.#'*[0-9]

distclean: clean
	$(MAKE) -C libqhtml distclean
	rm -rf config.h config.mak

install: $(TARGETS) qe.1
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 -d $(DESTDIR)$(mandir)/man1
	$(INSTALL) -m 755 -d $(DESTDIR)$(datadir)/qe
ifdef CONFIG_X11
	$(INSTALL) -m 755 -s xqe$(EXE) $(DESTDIR)$(prefix)/bin/qemacs$(EXE)
else
  ifdef CONFIG_TINY
	$(INSTALL) -m 755 -s tqe$(EXE) $(DESTDIR)$(prefix)/bin/qemacs$(EXE)
  else
	$(INSTALL) -m 755 -s qe$(EXE) $(DESTDIR)$(prefix)/bin/qemacs$(EXE)
  endif
endif
	ln -sf qemacs$(EXE) $(DESTDIR)$(prefix)/bin/qe$(EXE)
ifdef CONFIG_FFMPEG
	ln -sf qemacs$(EXE) $(DESTDIR)$(prefix)/bin/ffplay$(EXE)
endif
	$(INSTALL) -m 644 kmaps ligatures $(DESTDIR)$(datadir)/qe
	$(INSTALL) -m 644 qe.1 $(DESTDIR)$(mandir)/man1
ifdef CONFIG_HTML
	$(INSTALL) -m 755 -s html2png$(EXE) $(DESTDIR)$(prefix)/bin
endif

uninstall:
	rm -f $(DESTDIR)$(prefix)/bin/qemacs$(EXE)   \
	      $(DESTDIR)$(prefix)/bin/qe$(EXE)       \
	      $(DESTDIR)$(prefix)/bin/tqe$(EXE)      \
	      $(DESTDIR)$(prefix)/bin/xqe$(EXE)      \
	      $(DESTDIR)$(prefix)/bin/ffplay$(EXE)   \
	      $(DESTDIR)$(mandir)/man1/qe.1          \
	      $(DESTDIR)$(datadir)/qe/kmaps          \
	      $(DESTDIR)$(datadir)/qe/ligatures      \
	      $(DESTDIR)$(prefix)/bin/html2png$(EXE)

rebuild:
	./configure && $(MAKE) clean all

TAGS: force
	etags *.[ch]

colortest:
	tests/16colors.pl
	tests/256colors2.pl
	tests/truecolors.sh
	tests/color-spaces.pl
	tests/mandelbrot.sh
	tests/xterm-colour-chart.py
	tests/7936-colors.sh

help:
	@echo "Usage: make [targets] [BUILD_ALL=1] [DEBUG=1] [VERBOSE=1]"
	@echo "targets:"
	@echo "  all [default]: build the distribution files for all configured versions"
	@echo "  qe: build the terminal version qe"
	@echo "  xqe: build the X11 version xqe"
	@echo "  tqe: build the tiny version tqe"
	@echo "  debug: build an unoptimized debug version of qe named qe_debug"
	@echo "  xxx_debug: build an unoptimized debug version of the xxx target"
	@echo "flags:"
	@echo "  BUILD_ALL=1  rebuild some distribution files: ligatures kmaps charsets"
	@echo "  VERBOSE=1    show complete commands instead of abbreviated ones"

force:

#
# tar archive for distribution
#
FILES:= $(shell git ls-files)
FILE=qemacs-$(shell cat VERSION)

tar: $(FILES)
	rm -rf /tmp/$(FILE)
	mkdir -p /tmp/$(FILE)
	tar cf - $(FILES) | (cd /tmp/$(FILE) ; tar xf - )
	( cd /tmp ; tar cfz - $(FILE) ) > ../$(FILE).tar.gz
	rm -rf /tmp/$(FILE)

archive:
	git archive --prefix=$(FILE)/ HEAD | gzip > ../$(FILE).tar.gz

SPLINTOPTS := -DSPLINT +posixlib -nestcomment +boolint +charintliteral -mayaliasunique
SPLINTOPTS += -nullstate -unqualifiedtrans +charint
# extra options that will be removed later
SPLINTOPTS += -mustfreeonly -temptrans -kepttrans -DSTBI_NO_SIMD

splint: $(OBJS_DIR)/modules.txt
	splint $(SPLINTOPTS) -I. -Ilibqhtml -I$(OBJS_DIR) $(SRCS)
