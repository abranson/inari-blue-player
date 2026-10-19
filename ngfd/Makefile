PREFIX ?= /usr
LIBDIR ?= $(PREFIX)/lib
PLUGIN_DIR ?= $(LIBDIR)/ngf
TESTDIR ?= /opt/tests/ngfd-plugin-inari-blue

CC ?= cc
PKG_CONFIG ?= pkg-config

NGF_CFLAGS := $(shell $(PKG_CONFIG) --cflags ngf-plugin)
NGF_INCLUDE_FLAGS := $(NGF_CFLAGS) $(patsubst -I%, -I%/..,$(filter -I%,$(NGF_CFLAGS)))
GLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 dbus-1 mce)
PLUGIN_LIBS := $(shell $(PKG_CONFIG) --libs glib-2.0 dbus-1 mce)
TEST_CFLAGS := $(shell $(PKG_CONFIG) --cflags check glib-2.0 dbus-1 mce)
TEST_LIBS := $(shell $(PKG_CONFIG) --libs check glib-2.0 dbus-1 mce)

CFLAGS ?= -O2 -Wall -Wextra

.PHONY: all check install install-tests clean uninstall

all: libngfd_inari-blue.so

libngfd_inari-blue.so: plugin.o
	$(CC) $(LDFLAGS) -shared -Wl,-soname,$@ -o $@ $^ $(PLUGIN_LIBS)

plugin.o: plugin.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(NGF_INCLUDE_FLAGS) $(GLIB_CFLAGS) -fPIC -c -o $@ $<

test-inari-blue: test-inari-blue.c plugin.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(NGF_INCLUDE_FLAGS) $(TEST_CFLAGS) $(LDFLAGS) -o $@ test-inari-blue.c $(TEST_LIBS)

check: test-inari-blue
	./test-inari-blue

install: all
	install -D -m 0755 libngfd_inari-blue.so $(DESTDIR)$(PLUGIN_DIR)/libngfd_inari-blue.so

install-tests: test-inari-blue
	install -D -m 0755 test-inari-blue $(DESTDIR)$(TESTDIR)/test-inari-blue

uninstall:
	rm -f $(DESTDIR)$(PLUGIN_DIR)/libngfd_inari-blue.so
	rm -f $(DESTDIR)$(TESTDIR)/test-inari-blue

clean:
	rm -f libngfd_inari-blue.so plugin.o test-inari-blue
