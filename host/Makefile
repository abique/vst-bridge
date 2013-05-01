include ../config.mk

SRC    = host.cc

all: vst-bridge-host-32.exe vst-bridge-host-64.exe

vst-bridge-host-32.exe: $(SRC) ../common/common.h ../config.h
	$(WINCXX) -m32 $(CPPFLAGS) $(CFLAGS) $(SRC) -lpthread -o $@

vst-bridge-host-64.exe: $(SRC) ../common/common.h ../config.h
	$(WINCXX) -m64 $(CPPFLAGS) $(CFLAGS) $(SRC) -lpthread -o $@

clean:
	rm -f vst-bridge-host-*.exe*

install: all
	install -m 755 -d $(DESTDIR)$(PREFIX)/lib/vst-bridge
	install -m 755 vst-bridge-host-*.exe* $(DESTDIR)$(PREFIX)/lib/vst-bridge/
