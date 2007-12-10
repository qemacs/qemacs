# QEmacs, tiny but powerful multimode editor
#
# Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
# Copyright (c) 2002-2007 Charlie Gordon.
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

# from configure
#CONFIG_NETWORK=yes
#CONFIG_WIN32=yes
#CONFIG_CYGWIN=yes
#CONFIG_X11=yes
#CONFIG_XV=yes
#CONFIG_XRENDER=yes
#CONFIG_TINY=yes
#CONFIG_XFT=yes
#CONFIG_HTML=yes
#CONFIG_DLL=yes
#CONFIG_INIT_CALLS=yes
#CONFIG_PNG_OUTPUT=yes
#CONFIG_FFMPEG=yes

# currently fixes
#
# Define CONFIG_ALL_MODES to include all edit modes
#
CONFIG_ALL_MODES=y
#
# Define CONFIG_UNICODE_JOIN to include unicode bidi/script handling
#
CONFIG_UNICODE_JOIN=y
# 
# Define CONFIG_ALL_KMAPS it to include generic key map handling
#
CONFIG_ALL_KMAPS=y

CFLAGS:= -Wall -g $(CFLAGS) -funsigned-char

-include cflags.mk

ifdef TARGET_GPROF
CFLAGS+= -p
LDFLAGS+= -p
endif

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

ifdef CONFIG_TINY
CONFIG_X11=
CONFIG_ALL_MODES=
CONFIG_UNICODE_JOIN=
CONFIG_ALL_KMAPS=
CONFIG_HTML=
CONFIG_DOCBOOK=
CONFIG_DLL=
endif

ifdef CONFIG_WIN32
CONFIG_ALL_KMAPS=
CONFIG_X11=
CONFIG_DLL=
EXE=.exe
endif

ifdef CONFIG_PNG_OUTPUT
HTMLTOPPM_LIBS+= -lpng
endif

ifdef CONFIG_ALL_KMAPS
DEFINES+= -DCONFIG_ALL_KMAPS
endif

ifdef CONFIG_UNICODE_JOIN
DEFINES+= -DCONFIG_UNICODE_JOIN
endif

ifdef CONFIG_ALL_MODES
DEFINES+= -DCONFIG_ALL_MODES
endif

ifdef CONFIG_DLL
LIBS+=-ldl
# export some qemacs symbols
LDFLAGS+=-Wl,-E
endif

LIBS+=-lm

TARGETLIBS:=
TARGETS+=qe$(EXE) qe-doc.html kmaps ligatures

OBJS=qe.o charset.o buffer.o \
     input.o unicode_join.o display.o util.o hex.o list.o 

ifndef CONFIG_FFMPEG
OBJS+= cutils.o
endif

ifndef CONFIG_WIN32
OBJS+= unix.o tty.o 
endif

# more charsets if needed
ifndef CONFIG_TINY
OBJS+=charsetjis.o charsetmore.o
endif

ifdef CONFIG_ALL_MODES
OBJS+= unihex.o clang.o latex-mode.o xml.o bufed.o
ifndef CONFIG_WIN32
OBJS+= shell.o dired.o 
endif
endif

ifdef CONFIG_WIN32
OBJS+= win32.o
LIBS+= -lgdi32
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

ifdef CONFIG_UNICODE_JOIN
OBJS+= arabic.o indic.o qfribidi.o unihex.o
endif

ifdef CONFIG_FFMPEG
OBJS+= video.o image.o
DEP_LIBS+=$(FFMPEG_LIBDIR)/libavcodec/libavcodec.a $(FFMPEG_LIBDIR)/libavformat/libavformat.a
LIBS+=  -L$(FFMPEG_LIBDIR)/libavcodec -L$(FFMPEG_LIBDIR)/libavformat -lavformat -lavcodec -lz -lpthread
DEFINES+= -I$(FFMPEG_SRCDIR)/libavcodec -I$(FFMPEG_SRCDIR)/libavformat
TARGETS+= ffplay$(EXE)
endif

# must be the last object
OBJS+= qeend.o

all: $(TARGETLIBS) $(TARGETS)

libqhtml: force
	make -C libqhtml all

qe_g$(EXE): $(OBJS) $(DEP_LIBS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

qe$(EXE): qe_g$(EXE)
	rm -f $@
	cp $< $@
	$(STRIP) $@
	@ls -l $@

ffplay$(EXE): qe$(EXE)
	ln -sf $< $@

qe.o: qe.c qe.h qfribidi.h qeconfig.h

charset.o: charset.c qe.h
charsetjis.o: charsetjis.c qe.h charsetjis.def
charsetmore.o: charsetmore.c qe.h

buffer.o: buffer.c qe.h

tty.o: tty.c qe.h

qfribidi.o: qfribidi.c qfribidi.h

cfb.o: cfb.c cfb.h fbfrender.h

fbfrender.o: fbfrender.c fbfrender.h libfbf.h

html2png.o: html2png.c qe.h

%.o : %.c
	$(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

clean:
	make -C libqhtml clean
	rm -f *.o *.exe *~ TAGS gmon.out core *.exe.stackdump \
           qe qe_g qfribidi kmaptoqe ligtoqe html2png fbftoqe fbffonts.c \
           cptoqe jistoqe

distclean: clean
	rm -f config.h config.mak

install: $(TARGETS) qe.1
	install -m 755 qe$(EXE) $(prefix)/bin/qemacs$(EXE)
	ln -sf qemacs $(prefix)/bin/qe$(EXE)
ifdef CONFIG_FFMPEG
	ln -sf qemacs$(EXE) $(prefix)/bin/ffplay$(EXE)
endif
	mkdir -p $(prefix)/share/qe
	install kmaps ligatures $(prefix)/share/qe
	install qe.1 $(prefix)/man/man1
ifdef CONFIG_HTML
	install -m 755 -s html2png$(EXE) $(prefix)/bin
endif

TAGS: force
	etags *.[ch]

force:

#
# tar archive for distribution
#

FILES=Changelog COPYING README TODO qe.1 config.eg \
      Makefile qe.tcc qemacs.spec \
      hex.c charset.c qe.c qe.h tty.c \
      html.c indic.c unicode_join.c input.c qeconfig.h \
      qeend.c unihex.c arabic.c kmaptoqe.c util.c \
      bufed.c qestyles.h x11.c buffer.c ligtoqe.c \
      qfribidi.c clang.c latex-mode.c xml.c dired.c list.c qfribidi.h html2png.c \
      charsetmore.c charsetjis.c charsetjis.def cptoqe.c jistoqe.c \
      libfbf.c fbfrender.c cfb.c fbftoqe.c libfbf.h fbfrender.h cfb.h \
      display.c display.h mpeg.c shell.c \
      docbook.c unifont.lig kmaps xterm-146-dw-patch \
      ligatures qe-doc.texi qe-doc.html \
      tests/HELLO.txt tests/TestPage.txt tests/test-hebrew \
      tests/test-capital-rtl tests/test-capital-rtl.ref \
      tests/testbidi.html \
      plugin-example/Makefile  plugin-example/my_plugin.c \
      image.c video.c win32.c configure VERSION \
      cutils.c cutils.h unix.c

# qhtml library
FILES+=libqhtml/Makefile libqhtml/css.c libqhtml/cssid.h \
       libqhtml/cssparse.c libqhtml/xmlparse.c libqhtml/htmlent.h \
       libqhtml/css.h libqhtml/csstoqe.c \
       libqhtml/docbook.css libqhtml/html.css 

# fonts
FILES+=fonts/fixed10.fbf  fonts/fixed12.fbf  fonts/fixed13.fbf  fonts/fixed14.fbf \
       fonts/helv10.fbf   fonts/helv12.fbf   fonts/helv14.fbf   fonts/helv18.fbf \
       fonts/helv24.fbf   fonts/helv8.fbf    fonts/times10.fbf  fonts/times12.fbf \
       fonts/times14.fbf  fonts/times18.fbf  fonts/times24.fbf  fonts/times8.fbf \
       fonts/unifont.fbf

FILE=qemacs-$(VERSION)

tar:
	rm -rf /tmp/$(FILE)
	mkdir -p /tmp/$(FILE)
	cp --parents $(FILES) /tmp/$(FILE)
	( cd /tmp ; tar zcvf $(HOME)/$(FILE).tar.gz $(FILE) )
	rm -rf /tmp/$(FILE)

#
# Test for bidir algorithm
#
qfribidi$(EXE): qfribidi.c
	$(HOST_CC) $(CFLAGS) -DTEST -o $@ $<

#
# build ligature table
#
ligtoqe$(EXE): ligtoqe.c
	$(HOST_CC) $(CFLAGS) -o $@ $<

ifdef BUILD_ALL
ligatures: ligtoqe$(EXE) unifont.lig
	./ligtoqe unifont.lig $@
endif

#
# Key maps build (Only useful if you want to build your own maps from yudit maps)
#
KMAPS=Arabic.kmap ArmenianEast.kmap ArmenianWest.kmap Chinese-CJ.kmap \
      Cyrillic.kmap Czech.kmap DE-RU.kmap Danish.kmap Dutch.kmap \
      Esperanto.kmap Ethiopic.kmap French.kmap Georgian.kmap German.kmap \
      Greek.kmap GreekMono.kmap Guarani.kmap HebrewP.kmap \
      Hungarian.kmap \
      KOI8_R.kmap Lithuanian.kmap Mnemonic.kmap Polish.kmap \
      Russian.kmap SGML.kmap TeX.kmap Troff.kmap VNtelex.kmap \
      Vietnamese.kmap XKB_iso8859-4.kmap \
      DanishAlternate.kmap GreekBible.kmap Polytonic.kmap Spanish.kmap \
      Thai.kmap VietnameseTelex.kmap Welsh.kmap \
      Hebrew.kmap HebrewIsraeli.kmap HebrewP.kmap Israeli.kmap Yiddish.kmap \
      Kana.kmap 
#     Hangul.kmap Hangul2.kmap Hangul3.kmap Unicode2.kmap 
#KMAPS_DIR=$(prefix)/share/yudit/data
KMAPS_DIR=kmap
KMAPS:=$(addprefix $(KMAPS_DIR)/, $(KMAPS))

kmaptoqe$(EXE): kmaptoqe.c
	$(HOST_CC) $(CFLAGS) -o $@ $<

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

cptoqe$(EXE): cptoqe.c
	$(HOST_CC) $(CFLAGS) -o $@ $<

jistoqe$(EXE): jistoqe.c
	$(HOST_CC) $(CFLAGS) -o $@ $<

ifdef BUILD_ALL
charsetmore.c: cp/cpdata.txt $(CP) cptoqe$(EXE) Makefile
	./cptoqe -i cp/cpdata.txt $(CP) > $@

charsetjis.def: $(JIS) jistoqe$(EXE) Makefile
	./jistoqe $(JIS) > $@
endif

#
# fonts (only needed for html2png)
#
FONTS=fixed10.fbf fixed12.fbf fixed13.fbf fixed14.fbf\
      helv8.fbf helv10.fbf helv12.fbf helv14.fbf helv18.fbf helv24.fbf\
      times8.fbf times10.fbf times12.fbf times14.fbf times18.fbf times24.fbf\
      unifont.fbf
FONTS:=$(addprefix fonts/,$(FONTS))

fbftoqe$(EXE): fbftoqe.o cutils.o
	$(CC) $(CFLAGS) -o $@ $^

fbffonts.c: fbftoqe$(EXE) $(FONTS)
	./fbftoqe $(FONTS) > $@

#
# html2png tool (XML/HTML/CSS2 renderer test tool)
#
OBJS=util.o cutils.o \
     arabic.o indic.o qfribidi.o display.o unicode_join.o \
     charset.o charsetmore.o charsetjis.o \
     libfbf.o fbfrender.o cfb.o fbffonts.o

html2png$(EXE): html2png.o $(OBJS) libqhtml/libqhtml.a
	$(HOST_CC) $(LDFLAGS) -o $@ html2png.o $(OBJS) \
                   -L./libqhtml -lqhtml $(HTMLTOPPM_LIBS)

# autotest target
test:
	make -C tests test

# documentation
qe-doc.html: qe-doc.texi
	texi2html -monolithic -number $<
