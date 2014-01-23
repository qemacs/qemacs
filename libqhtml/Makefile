# Libqhtml tiny HTML/CSS rendering library for QEmacs
#
# Copyright (c) 2000-2002 Fabrice Bellard.
# Copyright (c) 2000-2014 Charlie Gordon.
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

DEPTH=..

include $(DEPTH)/config.mak

ifeq ($(CC),gcc)
  CFLAGS   += -Wall -g -O2 -funsigned-char
  # do not warn about zero-length formats.
  CFLAGS   += -Wno-format-zero-length
  LDFLAGS  := -g
endif

#include local compiler configuration file
-include $(DEPTH)/cflags.mk

ifdef TARGET_GPROF
  CFLAGS  += -p
  LDFLAGS += -p
endif

ifdef TARGET_ARCH_X86
  #CFLAGS+=-fomit-frame-pointer
  ifeq ($(GCC_MAJOR),2)
    CFLAGS+=-m386 -malign-functions=0
  else
    CFLAGS+=-march=i386 -falign-functions=0
  endif
endif

CFLAGS+=-I$(DEPTH)

LIB= libqhtml.a
OBJS= css.o xmlparse.o cssparse.o html_style.o docbook_style.o

DEPENDS:= qe.h config.h cutils.h display.h qestyles.h config.mak
DEPENDS:= $(addprefix $(DEPTH)/, $(DEPENDS))

OBJS_DIR:= $(DEPTH)/.objs/libqhtml
OBJS:= $(addprefix $(OBJS_DIR)/, $(OBJS))

$(shell mkdir -p $(OBJS_DIR))

#
# Dependencies
#
all: $(LIB)

$(LIB): $(OBJS) Makefile
	rm -f $@
	$(AR) rc $@ $(OBJS)

$(OBJS_DIR)/%.o: %.c $(DEPENDS) Makefile
	$(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

#
# build default style sheet file
#
csstoqe$(EXE): csstoqe.c Makefile
	$(HOST_CC) $(CFLAGS) -o $@ $<

html_style.c: html.css csstoqe$(EXE) Makefile
	./csstoqe html_style < $< > $@

docbook_style.c: docbook.css csstoqe$(EXE) Makefile
	./csstoqe docbook_style < $< > $@

clean:
	rm -rf *.dSYM
	rm -f *~ *.o *.a *.exe *_g TAGS gmon.out core *.exe.stackdump \
          csstoqe html_style.c docbook_style.c \
	  $(OBJS_DIR)/*.o
