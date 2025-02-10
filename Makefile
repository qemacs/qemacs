# QEmacs, tiny but powerful multimode editor
#
# Copyright (c) 2000-2002 Fabrice Bellard.
# Copyright (c) 2000-2025 Charlie Gordon.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

DEPTH=.
OSNAME:=$(shell uname -s)

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
  CFLAGS += -Wint-conversion
  CFLAGS += -Wno-packed
else
ifneq ($(OSNAME),OpenBSD)
  CFLAGS += -Wno-unused-result
endif
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

ifeq ($(CC),$(HOST_CC))
  HOST_CFLAGS:=$(CFLAGS)
endif

DEFINES=-DHAVE_QE_CONFIG_H

########################################################
# do not modify after this

ifeq ($(CC),clang)
SANITIZE_CFLAGS := -fno-sanitize-recover=all -fno-omit-frame-pointer
else
SANITIZE_CFLAGS := -fno-omit-frame-pointer
endif
DEBUG_SUFFIX:=
ifdef DEBUG
$(info Building with debug info)
DEBUG_SUFFIX:=_debug
ECHO_CFLAGS += -DCONFIG_DEBUG
CFLAGS += -g -O0
LDFLAGS += -g -O0
endif
ifdef ASAN
$(info Building with ASan)
DEBUG_SUFFIX:=_asan
ECHO_CFLAGS += -DCONFIG_ASAN
CFLAGS += -D__ASAN__
CFLAGS += -fsanitize=address $(SANITIZE_CFLAGS) -g
LDFLAGS += -fsanitize=address $(SANITIZE_CFLAGS) -g
endif
ifdef MSAN
$(info Building with MSan)
DEBUG_SUFFIX:=_msan
ECHO_CFLAGS += -DCONFIG_MSAN
CFLAGS += -D__MSAN__
CFLAGS += -fsanitize=memory $(SANITIZE_CFLAGS) -g
LDFLAGS += -fsanitize=memory $(SANITIZE_CFLAGS) -g
endif
ifdef UBSAN
$(info Building with UBSan)
DEBUG_SUFFIX:=_ubsan
ECHO_CFLAGS += -DCONFIG_UBSAN
CFLAGS += -D__UBSAN__
CFLAGS += -fsanitize=undefined $(SANITIZE_CFLAGS) -g
LDFLAGS += -fsanitize=undefined $(SANITIZE_CFLAGS) -g
endif

TARGETLIBS:=

TOP:=0
ifeq (,$(TARGET))
TARGET:=qe
TARGETS:=kmaps ligatures tqe qe-manual.md
ifeq (,$(DEBUG_SUFFIX))
TOP:=1
endif
endif
ifeq (,$(TARGET_OBJ))
TARGET_OBJ:=$(TARGET)
endif

OBJS:= qe.o cutils.o util.o color.o charset.o buffer.o search.o input.o display.o \
       qescript.o modes/hex.o

ifdef CONFIG_32BIT
CFLAGS += -m32
LDFLAGS += -m32
endif

ifdef TARGET_TINY
ECHO_CFLAGS += -DCONFIG_TINY
CFLAGS += -DCONFIG_TINY -Os
#CFLAGS += -m32
#LDFLAGS += -m32
else
OBJS+= extras.o variables.o
endif

#ifdef CONFIG_DARWIN
#  LDFLAGS += -L/opt/local/lib/
#endif

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
  OBJS+= unicode_join.o arabic.o indic.o
  OBJS+= libunicode.o libregexp.o
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
       lang/sharp.o     lang/emf.o       lang/csv.o       lang/crystal.o   \
       lang/rye.o       lang/nanorc.o    lang/tcl.o       modes/fractal.o  \
       $(EXTRA_MODES)
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
endif	# TARGET_TINY

SRCS:= $(OBJS:.o=.c)

DEPENDS:= qe.h config.h config.mak charset.h color.h cutils.h display.h \
	qestyles.h unicode_join.h util.h variables.h \
	wcwidth.h lang/clang.h

DEPENDS:= $(addprefix $(DEPTH)/, $(DEPENDS))

BINDIR:=$(DEPTH)/bin

OBJS_DIR:= $(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/$(TARGET_OBJ)$(DEBUG_SUFFIX)
CFLAGS+= -I$(OBJS_DIR)
OBJS:= $(addprefix $(OBJS_DIR)/, $(OBJS))
OBJS+= $(OBJS_DIR)/$(TARGET)_modules.o

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

ifneq (,$(DEBUG_SUFFIX))
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
xqe:		force;	$(MAKE) TARGET=xqe TARGET_OBJ=qe TARGET_X11=1
tqe:		force;	$(MAKE) TARGET=tqe TARGET_TINY=1
tqe1:		force;	$(MAKE) TARGET=tqe TARGET_TINY=1 tqe1$(EXE)
asan qe_asan:	force;	$(MAKE) TARGET=qe ASAN=1
msan qe_msan:	force;	$(MAKE) TARGET=qe MSAN=1
ubsan qe_ubsan:	force;	$(MAKE) TARGET=qe UBSAN=1
debug qe_debug:	force;	$(MAKE) TARGET=qe DEBUG=1
xqe_debug:	force;	$(MAKE) TARGET=xqe TARGET_OBJ=qe TARGET_X11=1 DEBUG=1
tqe_debug:	force;	$(MAKE) TARGET=tqe TARGET_TINY=1 DEBUG=1

else

# Amalgation mode produces a larger executable
TSRCS:=qe.c cutils.c util.c color.c charset.c buffer.c search.c input.c display.c \
       modes/hex.c parser.c unix.c tty.c win32.c qeend.c
TSRCS+= $(OBJS_DIR)/tqe_modules.c

tqe1_g$(EXE): tqe.c $(TSRCS) Makefile
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

$(OBJS_DIR)/$(TARGET)_modules.o: $(OBJS_DIR)/$(TARGET)_modules.c Makefile
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

$(OBJS_DIR)/$(TARGET)_modules.c: $(SRCS) Makefile config.mak
	@echo creating $@
	$(cmd)  mkdir -p $(dir $@)
	@echo '/* This file was generated automatically */' > $@
	@echo '#include "qe.h"'                             >> $@
	@echo '#undef qe_module_init'                       >> $@
	@echo '#define qe_module_init(fn)  extern int qe_module_##fn(QEmacsState *qs)' >> $@
	-@grep -h ^qe_module_init $(SRCS)                   >> $@
	@echo '#undef qe_module_init'                       >> $@
	@echo 'void qe_init_all_modules(QEmacsState *qs) {' >> $@
	@echo '#define qe_module_init(fn)  qe_module_##fn(qs)' >> $@
	-@grep -h ^qe_module_init $(SRCS)                   >> $@
	@echo '#undef qe_module_init'                       >> $@
	@echo '}'                                           >> $@
	@echo '#undef qe_module_exit'                       >> $@
	@echo '#define qe_module_exit(fn)  extern void qe_module_##fn(QEmacsState *qs)' >> $@
	-@grep -h ^qe_module_exit $(SRCS)                   >> $@
	@echo '#undef qe_module_exit'                       >> $@
	@echo 'void qe_exit_all_modules(QEmacsState *qs) {' >> $@
	@echo '#define qe_module_exit(fn)  qe_module_##fn(qs)' >> $@
	-@grep -h ^qe_module_exit $(SRCS)                   >> $@
	@echo '#undef qe_module_exit'                       >> $@
	@echo '}'                                           >> $@

$(OBJS_DIR)/cfb.o: cfb.c cfb.h fbfrender.h
$(OBJS_DIR)/charset.o: charset.c wcwidth.c
$(OBJS_DIR)/charsetjis.o: charsetjis.c charsetjis.def
$(OBJS_DIR)/fbfrender.o: fbfrender.c fbfrender.h libfbf.h
$(OBJS_DIR)/modes/stb.o: modes/stb.c modes/stb_image.h
$(OBJS_DIR)/libunicode.o: libunicode.c libunicode.h libunicode-table.h
$(OBJS_DIR)/libregexp.o: libregexp.c libregexp.h libregexp-opcode.h

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
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

#
# build ligature table
#
$(BINDIR)/ligtoqe$(EXE): tools/ligtoqe.c cutils.c
	$(echo) CC -o $@ $^
	$(cmd)  mkdir -p $(dir $@)
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
	$(cmd)  mkdir -p $(dir $@)
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
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

$(BINDIR)/jistoqe$(EXE): tools/jistoqe.c cutils.c
	$(echo) CC $^
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

ifdef BUILD_ALL
charsetmore.c: cp/cpdata.txt $(CP) $(BINDIR)/cptoqe$(EXE) Makefile
	$(BINDIR)/cptoqe -i cp/cpdata.txt $(CP) > $@

charsetjis.def: $(JIS) $(BINDIR)/jistoqe$(EXE) Makefile
	$(BINDIR)/jistoqe $(JIS) > $@
endif

#
# Unicode tables
#

UNICODE_VER=15.0.0

$(BINDIR)/unitable$(EXE): tools/unitable.c cutils.h wcwidth.h wcwidth.c
	$(echo) CC tools/unitable.c
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ tools/unitable.c

bidir_tables: $(BINDIR)/unicode_gen$(EXE) Makefile
	$(BINDIR)/unicode_gen -V $(UNICODE_VER) -i bidir_tables.h -c bidir_tables.c -b -a

wcwidth: $(BINDIR)/unicode_gen$(EXE) Makefile
	$(BINDIR)/unicode_gen -V $(UNICODE_VER) -i wcwidth.h -c wcwidth.c -3 -w -S

wcwidth_1: $(BINDIR)/unicode_gen$(EXE) Makefile
	$(BINDIR)/unicode_gen -V $(UNICODE_VER) -i wcwidth_1.h -c wcwidth_1.c -W

check_width: $(BINDIR)/unitable$(EXE) Makefile
	$(BINDIR)/unitable -V $(UNICODE_VER) -C -a > check_width.txt

unitable.txt: $(BINDIR)/unitable$(EXE) Makefile
	$(BINDIR)/unitable -V $(UNICODE_VER) > unitable.txt

unicode_width: $(BINDIR)/unitable$(EXE) Makefile
	$(BINDIR)/unitable -V $(UNICODE_VER) -W -a > unicode_width.h
#
#libunicode_table: $(BINDIR)/unicode_gen$(EXE) Makefile
#	$(BINDIR)/unicode_gen libunicode-table.h

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
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $^

fbffonts.c: $(BINDIR)/fbftoqe$(EXE) $(FONTS)
	$(BINDIR)/fbftoqe $(FONTS) > $@

#
# html2png tool (XML/HTML/CSS2 renderer test tool)
#
OBJS1=html2png.o cutils.o util.o color.o display.o \
      arabic.o indic.o unicode_join.o \
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
qe-manual.md: $(BINDIR)/scandoc$(EXE) qe-manual.c $(SRCS) $(DEPENDS) Makefile
	$(BINDIR)/scandoc$(EXE) qe-manual.c $(SRCS) $(DEPENDS) > $@

qe-doc.html: qe-doc.texi Makefile
	LANGUAGE=en_US LC_ALL=en_US.UTF-8 texi2html -monolithic $<
	@mv $@ $@.tmp
	@sed "s/This document was generated on.*//"     < $@.tmp | \
		sed "s/<!-- Created on .* by/<!-- Created by/" > $@
	@rm $@.tmp

qe-doc.info: qe-doc.texi Makefile
	LANGUAGE=en_US LC_ALL=en_US.UTF-8 makeinfo -o $@ $<

qe-doc.pdf: qe-doc.texi Makefile
	LANGUAGE=en_US LC_ALL=en_US.UTF-8 texi2pdf -o $@ $<

#
# Maintenance targets
#
clean:
	$(MAKE) -C libqhtml clean
	rm -f qe-doc.aux qe-doc.info qe-doc.log qe-doc.pdf qe-doc.toc
	rm -rf *.dSYM *.gch .objs* .tobjs* .xobjs* bin
	rm -f *~ *.o *.a *.exe *_g *_debug TAGS gmon.out core *.exe.stackdump \
           qe tqe tqe1 xqe kmaptoqe ligtoqe html2png cptoqe jistoqe \
           fbftoqe fbffonts.c allmodules.txt basemodules.txt '.#'*[0-9] \
           qe_asan qe_msan qe_ubsan

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
FILE=qemacs-$(shell cat VERSION)

archive:
	git archive --prefix=$(FILE)/ HEAD | gzip > ../$(FILE).tar.gz

SPLINTOPTS := -DSPLINT +posixlib -nestcomment +boolint +charintliteral -mayaliasunique
SPLINTOPTS += -nullstate -unqualifiedtrans +charint
# extra options that will be removed later
SPLINTOPTS += -mustfreeonly -temptrans -kepttrans -DSTBI_NO_SIMD

splint:
	splint $(SPLINTOPTS) -I. -Ilibqhtml -I$(OBJS_DIR) $(SRCS)
