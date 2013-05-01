include ../config.mk

TARGET = vst-bridge-maker
SRC = maker.c

$(TARGET): $(SRC) ../common/common.h ../config.h
	$(CC) $(CFLAGS) $(SRC) -o $@

install: $(TARGET)
	install -m 755 -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(TARGET) $(DESTDIR)$(PREFIX)/bin

clean:
	rm $(TARGET)
