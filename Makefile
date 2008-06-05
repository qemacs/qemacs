# QEmacs, tiny but powerful multimode editor
#
# Copyright (c) 2000-2002 Fabrice Bellard.
# Copyright (c) 2000-2008 Charlie Gordon.
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

include config.mak

ifeq ($(CC),gcc)
  CFLAGS   := -Wall -g -O2 -funsigned-char
  # do not warn about zero-length formats.
  CFLAGS   += -Wno-format-zero-length
  LDFLAGS  := -g
endif

#include local compiler configuration file
-include cflags.mk

ifdef TARGET_GPROF
  CFLAGS   += -p
  LDFLAGS  += -p
endif

TLDFLAGS := $(LDFLAGS)

ifdef TARGET_ARCH_X86
  #CFLAGS+=-fomit-frame-pointer
  ifeq ($(GCC_MAJOR),2)
    CFLAGS+=-m386 -malign-functions=0
  else
    CFLAGS+=-march=i386 -falign-functions=0
  endif
endif

DEFINES=-DHAVE_QE_CONFIG_H

########################################################
# do not modify after this

TARGETLIBS:=
TARGETS+= qe$(EXE) tqe$(EXE) kmaps ligatures

OBJS= qe.o charset.o buffer.o input.o display.o util.o hex.o list.o cutils.o
TOBJS= tqe.o charset.o buffer.o input.o display.o util.o hex.o list.o cutils.o

OBJS+= extras.o

ifdef CONFIG_PNG_OUTPUT
  HTMLTOPPM_LIBS+= -lpng
endif

ifdef CONFIG_DLL
  LIBS+=-ldl
  # export some qemacs symbols
  LDFLAGS+=-Wl,-E
endif

ifdef CONFIG_DOC
  TARGETS+= qe-doc.html
endif

ifdef CONFIG_WIN32
  OBJS+= unix.o
  TOBJS+= unix.o
  OBJS+= win32.o
  TOBJS+= win32.o
#  OBJS+= printf.o
#  TOBJS+= printf.o
  LIBS+= -lmsvcrt -lgdi32 -lwsock32
  TLIBS+= -lmsvcrt -lgdi32 -lwsock32
else
  OBJS+= unix.o tty.o
  TOBJS+= unix.o tty.o
  LIBS+= -lm
endif

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
ifndef CONFIG_TINY
  OBJS+= charsetjis.o charsetmore.o
endif

ifdef CONFIG_ALL_MODES
  OBJS+= unihex.o clang.o xml.o bufed.o \
         lisp.o makemode.o perl.o htmlsrc.o script.o variables.o
  ifndef CONFIG_WIN32
    OBJS+= shell.o dired.o latex-mode.o
  endif
endif

# currently not used in qemacs
ifdef CONFIG_CFB
  OBJS+= libfbf.o fbfrender.o cfb.o fbffonts.o
endif

ifdef CONFIG_X11
  OBJS+= x11.o
  ifdef CONFIG_XRENDER
    LIBS+=-lXrender
  endif
  ifdef CONFIG_XV
    LIBS+=-lXv
  endif
  LIBS+= -L/usr/X11R6/lib -lXext -lX11
endif

ifdef CONFIG_HTML
  CFLAGS+=-I./libqhtml
  DEP_LIBS+=libqhtml/libqhtml.a
  LIBS+=-L./libqhtml -lqhtml
  OBJS+=html.o docbook.o
  ifndef CONFIG_WIN32
    TARGETLIBS+= libqhtml
    TARGETS+= html2png$(EXE)
  endif
endif

ifdef CONFIG_FFMPEG
  OBJS+= video.o image.o
  DEP_LIBS+= $(FFMPEG_LIBDIR)/libavcodec/libavcodec.a $(FFMPEG_LIBDIR)/libavformat/libavformat.a
  LIBS+= -L$(FFMPEG_LIBDIR)/libavcodec -L$(FFMPEG_LIBDIR)/libavformat -lavformat -lavcodec -lz -lpthread
  DEFINES+= -I$(FFMPEG_SRCDIR)/libavcodec -I$(FFMPEG_SRCDIR)/libavformat
  TARGETS+= ffplay$(EXE)
endif

ifdef CONFIG_INIT_CALLS
  # must be the last object
  OBJS+= qeend.o
  TOBJS+= qeend.o
endif

SRCS:= $(OBJS:.o=.c)
TSRCS:= $(TOBJS:.o=.c)
TSRCS:= $(TSRCS:tqe.c=qe.c)

OBJS_DIR:=.objs
OBJS:=$(addprefix $(OBJS_DIR)/, $(OBJS))
TOBJS:=$(addprefix $(OBJS_DIR)/, $(TOBJS))

$(shell mkdir -p $(OBJS_DIR))

#
# Dependencies
#
all: $(TARGETLIBS) $(TARGETS)

libqhtml: force
	$(MAKE) -C libqhtml all

qe_g$(EXE): $(OBJS) $(DEP_LIBS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

qe$(EXE): qe_g$(EXE) Makefile
	rm -f $@
	cp $< $@
	-$(STRIP) $@
	@ls -l $@
	echo `size $@` `wc -c $@` qe $(OPTIONS) \
		| cut -d ' ' -f 7-10,13,15-40 >> STATS

#
# Tiny version of QEmacs
#
tqe_g$(EXE): $(TOBJS)
	$(CC) $(TLDFLAGS) -o $@ $^ $(TLIBS)

tqe$(EXE): tqe_g$(EXE) Makefile
	rm -f $@
	cp $< $@
	-$(STRIP) $@
	@ls -l $@
	echo `size $@` `wc -c $@` tqe $(OPTIONS) \
		| cut -d ' ' -f 7-10,13,15-40 >> STATS

$(OBJS_DIR)/tqe.o: qe.c qe.h qestyles.h qeconfig.h config.h config.mak Makefile
	$(CC) $(DEFINES) -DCONFIG_TINY $(CFLAGS) -o $@ -c $<

ffplay$(EXE): qe$(EXE) Makefile
	ln -sf $< $@

ifndef CONFIG_INIT_CALLS
$(OBJS_DIR)/qe.o: allmodules.txt
$(OBJS_DIR)/tqe.o: basemodules.txt
endif

allmodules.txt: $(SRCS) Makefile
	@echo creating $@
	@echo '/* This file was generated automatically */' > $@
	@grep -h ^qe_module_init $(SRCS)                    >> $@

basemodules.txt: $(TSRCS) Makefile
	@echo creating $@
	@echo '/* This file was generated automatically */' > $@
	@grep -h ^qe_module_init $(TSRCS)                   >> $@

$(OBJS_DIR)/cfb.o: cfb.c cfb.h fbfrender.h
$(OBJS_DIR)/charsetjis.o: charsetjis.c charsetjis.def
$(OBJS_DIR)/fbfrender.o: fbfrender.c fbfrender.h libfbf.h
$(OBJS_DIR)/qe.o: qe.c qe.h qfribidi.h qeconfig.h
$(OBJS_DIR)/qfribidi.o: qfribidi.c qfribidi.h

$(OBJS_DIR)/%.o: %.c qe.h qestyles.h config.h config.mak Makefile
	$(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

#
# Test for bidir algorithm
#
qfribidi$(EXE): qfribidi.c cutils.c
	$(HOST_CC) $(CFLAGS) -DTEST -o $@ $^

#
# build ligature table
#
ligtoqe$(EXE): ligtoqe.c cutils.c
	$(HOST_CC) $(CFLAGS) -o $@ $^

ifdef BUILD_ALL
ligatures: ligtoqe$(EXE) unifont.lig
	./ligtoqe unifont.lig $@
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
#KMAPS_DIR=$(prefix)/share/yudit/data
KMAPS_DIR=kmap
KMAPS:=$(addprefix $(KMAPS_DIR)/, $(KMAPS))

kmaptoqe$(EXE): kmaptoqe.c cutils.c
	$(HOST_CC) $(CFLAGS) -o $@ $^

ifdef BUILD_ALL
kmaps: kmaptoqe$(EXE) $(KMAPS) Makefile
	./kmaptoqe $@ $(KMAPS)
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
     kamen.cp     KOI8-R.TXT   koi8_u.cp    TCVN.TXT     VISCII.TXT  \
     CP037.TXT    CP424.TXT    CP500.TXT    CP875.TXT    CP1026.TXT

CP:=$(addprefix cp/,$(CP))

JIS= JIS0208.TXT JIS0212.TXT
JIS:=$(addprefix cp/,$(JIS))

cptoqe$(EXE): cptoqe.c cutils.c
	$(HOST_CC) $(CFLAGS) -o $@ $^

jistoqe$(EXE): jistoqe.c cutils.c
	$(HOST_CC) $(CFLAGS) -o $@ $^

ifdef BUILD_ALL
charsetmore.c: cp/cpdata.txt $(CP) cptoqe$(EXE) Makefile
	./cptoqe -i cp/cpdata.txt $(CP) > $@

charsetjis.def: $(JIS) jistoqe$(EXE) Makefile
	./jistoqe $(JIS) > $@
endif

#
# fonts (only needed for html2png)
#
FONTS=fixed10.fbf fixed12.fbf fixed13.fbf fixed14.fbf \
      helv8.fbf helv10.fbf helv12.fbf helv14.fbf helv18.fbf helv24.fbf \
      times8.fbf times10.fbf times12.fbf times14.fbf times18.fbf times24.fbf \
      unifont.fbf
FONTS:=$(addprefix fonts/,$(FONTS))

fbftoqe$(EXE): fbftoqe.c cutils.c
	$(HOST_CC) $(CFLAGS) -o $@ $^

fbffonts.c: fbftoqe$(EXE) $(FONTS)
	./fbftoqe $(FONTS) > $@

#
# html2png tool (XML/HTML/CSS2 renderer test tool)
#
OBJS1=html2png.o util.o cutils.o \
      arabic.o indic.o qfribidi.o display.o unicode_join.o \
      charset.o charsetmore.o charsetjis.o \
      libfbf.o fbfrender.o cfb.o fbffonts.o

OBJS1:=$(addprefix $(OBJS_DIR)/, $(OBJS1))

html2png$(EXE): $(OBJS1) libqhtml/libqhtml.a
	$(CC) $(LDFLAGS) -o $@ $(OBJS1) \
                   -L./libqhtml -lqhtml $(HTMLTOPPM_LIBS)

# autotest target
test:
	$(MAKE) -C tests test

# documentation
qe-doc.html: qe-doc.texi Makefile
	LANGUAGE=en_US LC_ALL=en_US.UTF-8 texi2html -monolithic -number $<

#
# Maintenance targets
#
clean:
	$(MAKE) -C libqhtml clean
	rm -f *~ *.o *.a *.exe *_g TAGS gmon.out core *.exe.stackdump   \
           qe tqe qfribidi kmaptoqe ligtoqe html2png fbftoqe fbffonts.c \
           cptoqe jistoqe allmodules.txt basemodules.txt '.#'*[0-9] \
	   $(OBJS_DIR)/*.o

distclean: clean
	rm -rf config.h config.mak $(OBJS_DIR)

install: $(TARGETS) qe.1
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/bin
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/man/man1
	$(INSTALL) -m 755 -d $(DESTDIR)$(prefix)/share/qe
	$(INSTALL) -m 755 -s qe$(EXE) $(DESTDIR)$(prefix)/bin/qemacs$(EXE)
	ln -sf qemacs $(DESTDIR)$(prefix)/bin/qe$(EXE)
ifdef CONFIG_FFMPEG
	ln -sf qemacs$(EXE) $(DESTDIR)$(prefix)/bin/ffplay$(EXE)
endif
	$(INSTALL) -m 644 kmaps ligatures $(DESTDIR)$(prefix)/share/qe
	$(INSTALL) -m 644 qe.1 $(DESTDIR)$(prefix)/man/man1
ifdef CONFIG_HTML
	$(INSTALL) -m 755 -s html2png$(EXE) $(DESTDIR)$(prefix)/bin
endif

uninstall:
	rm -f $(DESTDIR)$(prefix)/bin/qemacs$(EXE)   \
	      $(DESTDIR)$(prefix)/bin/qe$(EXE)       \
	      $(DESTDIR)$(prefix)/bin/ffplay$(EXE)   \
	      $(DESTDIR)$(prefix)/man/man1/qe.1      \
	      $(DESTDIR)$(prefix)/share/qe/kmaps     \
	      $(DESTDIR)$(prefix)/share/qe/ligatures \
	      $(DESTDIR)$(prefix)/bin/html2png$(EXE)

rebuild:
	./configure && $(MAKE) clean all

TAGS: force
	etags *.[ch]

force:

#
# tar archive for distribution
#
FILES:=COPYING Changelog Makefile README TODO VERSION               \
       arabic.c bufed.c buffer.c cfb.c cfb.h charset.c charsetjis.c \
       charsetjis.def charsetmore.c clang.c config.eg config.h      \
       configure cptoqe.c cutils.c cutils.h dired.c display.c       \
       display.h docbook.c extras.c fbffonts.c fbfrender.c          \
       fbfrender.h fbftoqe.c hex.c html.c html2png.c htmlsrc.c      \
       image.c indic.c input.c jistoqe.c kmap.c kmaptoqe.c          \
       latex-mode.c libfbf.c libfbf.h ligtoqe.c list.c makemode.c   \
       mpeg.c perl.c qe-doc.html qe-doc.texi qe.1 qe.c qe.h qe.tcc  \
       qeconfig.h qeend.c qemacs.spec qestyles.h qfribidi.c         \
       qfribidi.h script.c shell.c tty.c unicode_join.c unihex.c    \
       unix.c util.c variables.c variables.h video.c win32.c x11.c  \
       xml.c xterm-146-dw-patch

FILES+=plugins/Makefile  plugins/my_plugin.c

FILES+=tests/HELLO.txt tests/TestPage.txt tests/test-hebrew         \
       tests/test-capital-rtl tests/test-capital-rtl.ref            \
       tests/testbidi.html \

# qhtml library
FILES+=libqhtml/Makefile libqhtml/css.c libqhtml/css.h libqhtml/cssid.h \
       libqhtml/cssparse.c libqhtml/csstoqe.c libqhtml/docbook.css      \
       libqhtml/html.css libqhtml/htmlent.h libqhtml/xmlparse.c

# keyboard maps, code pages, fonts
FILES+=unifont.lig ligatures kmaps $(KMAPS) $(CP) $(JIS) $(FONTS)

FILE=qemacs-$(VERSION)

tar: $(FILES)
	rm -rf /tmp/$(FILE)
	mkdir -p /tmp/$(FILE)
	cp --parents $(FILES) /tmp/$(FILE)
	( cd /tmp ; tar zcvf $(HOME)/$(FILE).tar.gz $(FILE) )
	rm -rf /tmp/$(FILE)

SPLINTOPTS := +posixlib -nestcomment +boolint +charintliteral -mayaliasunique
SPLINTOPTS += -nullstate -unqualifiedtrans +charint
# extra options that will be removed later
SPLINTOPTS += -mustfreeonly -temptrans -kepttrans

splint: allmodules.txt basemodules.txt
	splint $(SPLINTOPTS) -I. -Ilibqhtml $(SRCS)
