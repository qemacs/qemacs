# Libqhtml tiny HTML/CSS rendering library for QEmacs
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
  CFLAGS += -Wno-packed
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

DEBUG_SUFFIX:=
ifdef DEBUG
DEBUG_SUFFIX:=_debug
ECHO_CFLAGS += -DCONFIG_DEBUG
CFLAGS += -g -O0
LDFLAGS += -g -O0
endif
ifdef ASAN
DEBUG_SUFFIX:=_asan
ECHO_CFLAGS += -DCONFIG_ASAN
CFLAGS += -D__ASAN__
CFLAGS += -fsanitize=address -fno-sanitize-recover=all -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
ifdef MSAN
DEBUG_SUFFIX:=_msan
ECHO_CFLAGS += -DCONFIG_MSAN
CFLAGS += -D__MSAN__
CFLAGS += -fsanitize=memory -fno-sanitize-recover=all -fno-omit-frame-pointer
LDFLAGS += -fsanitize=memory -fno-sanitize-recover=all -fno-omit-frame-pointer
endif
ifdef UBSAN
DEBUG_SUFFIX:=_ubsan
ECHO_CFLAGS += -DCONFIG_UBSAN
CFLAGS += -D__UBSAN__
CFLAGS += -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
LDFLAGS += -fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
endif

LIB= $(DEPTH)/.objs/$(TARGET_OS)-$(TARGET_ARCH)-$(CC)/libqhtml$(DEBUG_SUFFIX).a
OBJS= css.o xmlparse.o cssparse.o html_style.o docbook_style.o

DEPENDS:= config.h config.mak charset.h color.h cutils.h display.h \
		unicode_join.h util.h
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
