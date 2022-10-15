# Libqhtml tiny HTML/CSS rendering library for QEmacs
#
# Copyright (c) 2000-2002 Fabrice Bellard.
# Copyright (c) 2000-2022 Charlie Gordon.
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

ifdef TARGET_ARCH_X86
  #CFLAGS+=-fomit-frame-pointer
  ifeq ($(GCC_MAJOR),2)
    CFLAGS += -m386 -malign-functions=0
  else
    CFLAGS += -march=i386 -falign-functions=0
  endif
endif

CFLAGS+=-I$(DEPTH)

HOST_CFLAGS:=$(CFLAGS)

ifdef DEBUG
DEBUG_SUFFIX:=_debug
ECHO_CFLAGS += -DCONFIG_DEBUG
CFLAGS += -g -O0
LDFLAGS += -g -O0
else
DEBUG_SUFFIX:=
endif

LIB= $(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/libqhtml$(DEBUG_SUFFIX).a
OBJS= css.o xmlparse.o cssparse.o html_style.o docbook_style.o

DEPENDS:= config.h config.mak charset.h color.h cutils.h display.h \
		qfribidi.h unicode_join.h util.h
DEPENDS:= $(addprefix $(DEPTH)/, $(DEPENDS))
DEPENDS+= css.h cssid.h htmlent.h

BINDIR:=$(DEPTH)/bin

OBJS_DIR:= $(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/libqhtml$(DEBUG_SUFFIX)
OBJS:= $(addprefix $(OBJS_DIR)/, $(OBJS))

#
# Dependencies
#
all: $(LIB)

$(LIB): $(OBJS) Makefile
	@rm -f $@
	$(AR) rc $@ $(OBJS)

$(OBJS_DIR)/%.o: %.c $(DEPENDS) Makefile
	$(echo) CC $(ECHO_CFLAGS) -c $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(CC) $(DEFINES) $(CFLAGS) -o $@ -c $<

#
# build default style sheet file
#
$(BINDIR)/csstoqe$(EXE): csstoqe.c Makefile
	$(echo) CC -o $@ $<
	$(cmd)  mkdir -p $(dir $@)
	$(cmd)  $(HOST_CC) $(HOST_CFLAGS) -o $@ $<

html_style.c: html.css $(BINDIR)/csstoqe$(EXE) Makefile
	$(BINDIR)/csstoqe html_style < $< > $@

docbook_style.c: docbook.css $(BINDIR)/csstoqe$(EXE) Makefile
	$(BINDIR)/csstoqe docbook_style < $< > $@

clean:
	rm -rf *.dSYM *.gch $(OBJS_DIR)
	rm -f *~ *.o *.a *.exe *_g TAGS gmon.out core *.exe.stackdump \
          csstoqe html_style.c docbook_style.c

distclean: clean
	rm -f $(LIB)
