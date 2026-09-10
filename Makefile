CC ?= cc
CXX ?= c++

CFLAGS ?= -O2
CXXFLAGS ?= -O2 -std=c++11
CPPFLAGS ?= -Ireconstruction -Ireconstruction/imd
LDFLAGS ?=

# ViewKit development tree discovery.
#
# Preferred installed layouts are /usr or /usr/local. VIEWKLASS_ROOT may be
# used for an uninstalled MaXX/ViewKlass tree, for example:
#
#   make VIEWKLASS_ROOT=/path/to/MaXX
#
# /opt/MaXX remains a compatibility fallback for older MaXX installs.
VIEWKLASS_ROOT ?=
VIEWKIT_INCLUDE_DIR ?=
VIEWKIT_LIBRARY_DIR ?=

VIEWKIT_INCLUDE_CANDIDATES = \
	$(if $(strip $(VIEWKLASS_ROOT)),$(VIEWKLASS_ROOT)/include) \
	/usr/local/include \
	/usr/include \
	/opt/MaXX/include \
	$(CURDIR)/MaXX/include \
	$(CURDIR)/../MaXX/include

VIEWKIT_LIBRARY_CANDIDATES = \
	$(if $(strip $(VIEWKLASS_ROOT)),$(VIEWKLASS_ROOT)/lib64 $(VIEWKLASS_ROOT)/lib) \
	/usr/local/lib64 \
	/usr/local/lib \
	/usr/lib64 \
	/usr/lib \
	/opt/MaXX/lib64 \
	/opt/MaXX/lib \
	$(CURDIR)/MaXX/lib64 \
	$(CURDIR)/MaXX/lib \
	$(CURDIR)/../MaXX/lib64 \
	$(CURDIR)/../MaXX/lib

ifeq ($(strip $(VIEWKIT_INCLUDE_DIR)),)
VIEWKIT_INCLUDE_DIR := $(firstword $(foreach d,$(VIEWKIT_INCLUDE_CANDIDATES),$(if $(wildcard $(d)/Vk/VkComponent.h),$(d))))
endif

ifeq ($(strip $(VIEWKIT_LIBRARY_DIR)),)
VIEWKIT_LIBRARY_DIR := $(firstword $(foreach d,$(VIEWKIT_LIBRARY_CANDIDATES),$(if $(or $(wildcard $(d)/libVk.so),$(wildcard $(d)/libVk.so.*),$(wildcard $(d)/libvk.so),$(wildcard $(d)/libvk.so.*)),$(d))))
endif

ifneq ($(strip $(VIEWKIT_INCLUDE_DIR)),)
CPPFLAGS += -I$(VIEWKIT_INCLUDE_DIR)
endif
ifneq ($(strip $(VIEWKIT_LIBRARY_DIR)),)
LDFLAGS += -L$(VIEWKIT_LIBRARY_DIR)
endif

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
LDLIBS ?= $(VIEWKIT_LIBS) -lXm -lX11 -lXt -lXpm -lXcursor -lXinerama -lpthread -lpam -ldl

TARGET = dvlogin
INSTDIR = /usr/local/bin

.PHONY: all clean install uninstall legacy-info check-viewkit viewkit-info

all: check-viewkit $(TARGET)

check-viewkit:
	@if [ -z "$(VIEWKIT_INCLUDE_DIR)" ]; then \
		echo "dvlogin: ViewKit headers not found (missing Vk/VkComponent.h)" >&2; \
		echo "dvlogin: install ViewKit development headers or run:" >&2; \
		echo "  make VIEWKLASS_ROOT=/path/to/MaXX" >&2; \
		exit 1; \
	fi

viewkit-info:
	@echo "VIEWKIT_INCLUDE_DIR=$(VIEWKIT_INCLUDE_DIR)"
	@echo "VIEWKIT_LIBRARY_DIR=$(VIEWKIT_LIBRARY_DIR)"
	@echo "VIEWKIT_LIBS=$(VIEWKIT_LIBS)"

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

%.o: %.c++
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)

install: check-viewkit $(TARGET)
	install -m 755 $(TARGET) $(INSTDIR)

uninstall:
	rm -f $(INSTDIR)/$(TARGET)

legacy-info:
	@echo "Set CLOGIN_USE_LEGACY_MOTIF=1 to run the old prototype greeter."
