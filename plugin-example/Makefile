#
# QEmacs sample Makefile to develop plugins
#
# QEmacs plugins can contain modes, bindings, or any other qemacs
# dynamically registerable resource.
#

# modify to get the path of the qemacs sources
QEMACS_PATH=../
# define the name of your plugin
LIB=my_plugin.so
# define the object files it contains
OBJS=my_plugin.o

INCLUDES=-I$(QEMACS_PATH)
DEFINES=-DQE_MODULE
DIST_LIB:=$(HOME)/.qe/$(LIB)
CC=gcc
CFLAGS=-O2 -Wall -g -fPIC $(INCLUDES) $(DEFINES)

# by default, the plugin is copied in ~/.qe/ some that qemacs can load
# it automatically.
all: $(DIST_LIB)

$(DIST_LIB): $(LIB)
	install -s -m 644 $< $@

install: $(DIST_LIB)

$(LIB): $(OBJS) Makefile
	$(CC) -shared -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o *.so *~
