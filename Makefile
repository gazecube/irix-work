CC ?= cc
CXX ?= c++

CFLAGS ?= -O2
CXXFLAGS ?= -O2 -std=c++11
CPPFLAGS ?= -Ireconstruction -Ireconstruction/imd

C_SOURCE = main.c
CXX_SOURCES = \
	pandora_runtime.c++ \
	reconstruction/PandoraViewKit.c++ \
	reconstruction/PandoraPresentation.c++ \
	reconstruction/FtiVectorIcon.c++ \
	reconstruction/ImdFtrVisualCompat.c++ \
	reconstruction/Base/PwdNameList.c++ \
	reconstruction/Filesystem/DirectoryCheck.c++ \
	reconstruction/imd/ImdString.c++ \
	reconstruction/imd/ImdIconPanel.c++ \
	reconstruction/imd/DragManager.c++ \
	reconstruction/imd/ViewPanelAdopt.c++ \
	reconstruction/imd/InvertedTeeRenderer.c++

C_OBJECTS = $(C_SOURCE:.c=.o)
CXX_OBJECTS = $(CXX_SOURCES:.c++=.o)
OBJECTS = $(C_OBJECTS) $(CXX_OBJECTS)

# MaXX ViewKit/SGI compatibility libraries. Override VIEWKIT_LIBS if your
# installation uses a non-default library split or search path.
VIEWKIT_LIBS ?= -lVk -lSgm
LDFLAGS ?=
LDLIBS ?= $(VIEWKIT_LIBS) -lXm -lX11 -lXt -lXpm -lXcursor -lXinerama -lpthread -lpam -ldl

TARGET = dvlogin
INSTDIR = /usr/local/bin

.PHONY: all clean install uninstall legacy-info

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.c++
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)

install: $(TARGET)
	install -m 755 $(TARGET) $(INSTDIR)

uninstall:
	rm -f $(INSTDIR)/$(TARGET)

legacy-info:
	@echo "Set CLOGIN_USE_LEGACY_MOTIF=1 to run the old prototype greeter."
