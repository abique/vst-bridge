include ../config.mk

TARGET = vst-bridge-plugin-tpl.so
SRC = plugin.cc

$(TARGET): $(SRC) ../common/common.h ../config.h
	$(CXX) -shared -fPIC $(SRC) -o $@

install: $(TARGET)
	install -m 755 -d $(DESTDIR)$(PREFIX)/lib/vst-bridge
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/lib/vst-bridge/

clean:
	rm -f $(TARGET)
