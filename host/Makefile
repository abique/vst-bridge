include ../config.mk

SRC    = host.cc

all: vst-bridge-host-32.exe vst-bridge-host-64.exe

vst-bridge-host-32.exe: $(SRC) ../common/common.h ../config.h
	$(WINCXX) -m32 $(CXXFLAGS) $(SRC) -lpthread -lshell32 -lws2_32 -lX11 -o $@

vst-bridge-host-64.exe: $(SRC) ../common/common.h ../config.h
	$(WINCXX) -m64 $(CXXFLAGS) $(SRC) -lpthread -lshell32 -lws2_32 -lX11 -o $@

clean:
	rm -f vst-bridge-host-*.exe*

install: all
	install -m 755 -d $(DESTDIR)$(PREFIX)/lib/vst-bridge
	install -m 755 vst-bridge-host-*.exe* $(DESTDIR)$(PREFIX)/lib/vst-bridge/
