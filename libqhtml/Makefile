# Libqhtml tiny HTML/CSS rendering library for QEmacs
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

DEPTH=..

include $(DEPTH)/config.mak

CFLAGS:= -Wall -g $(CFLAGS) -funsigned-char

-include $(DEPTH)/cflags.mk

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

CFLAGS+=-I$(DEPTH)

LIB=libqhtml.a
OBJS=css.o xmlparse.o cssparse.o html_style.o docbook_style.o

OBJS_DIR:=$(DEPTH)/.objs/libqhtml
OBJS:=$(addprefix $(OBJS_DIR)/, $(OBJS))
$(shell mkdir -p $(OBJS_DIR))

all: $(LIB)

$(LIB): $(OBJS)
	rm -f $@
	$(AR) rc $@ $(OBJS)

$(OBJS_DIR)/%.o: %.c Makefile
	$(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

#
# build default style sheet file
#
csstoqe$(EXE): csstoqe.c
	$(HOST_CC) $(CFLAGS) -o $@ $<

html_style.c: html.css csstoqe$(EXE)
	./csstoqe html_style < $< > $@

docbook_style.c: docbook.css csstoqe$(EXE)
	./csstoqe docbook_style < $< > $@

clean:
	rm -f *~ *.o *.a *.exe *_g TAGS gmon.out core *.exe.stackdump \
          csstoqe html_style.c docbook_style.c \
	  $(OBJS_DIR)/*.o
