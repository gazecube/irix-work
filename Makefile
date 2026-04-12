COMPILER = cc
SOURCE = main.c
#CFLAGS = -g -ggdb -O0 -Wall -Werror -std=gnu99 -pedantic
CFLAGS = -O3
LDFLAGS = -lXm -lX11 -lXt -lXpm -lXcursor -lXinerama -lpthread -lpam
TARGET = ./dvlogin
INSTDIR = /usr/local/bin

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(COMPILER) $(CFLAGS) $^ $(LDFLAGS) -o $@
clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -m 755 $(TARGET) $(INSTDIR)

uninstall:
	rm -f $(INSTDIR)/$(notdir $(TARGET))

