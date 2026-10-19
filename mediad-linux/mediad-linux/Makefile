CXX ?= g++
CC ?= gcc
CPPFLAGS += -Iinclude -Isrc -I../irix-device-ecosystem/include -I/usr/include/tirpc -include linux_compat.h -D_COMPILER_VERSION=730 -D_GNU_SOURCE
CXXFLAGS += -std=gnu++98 -fpermissive -fPIC -g -O1 -Wall -Wno-parentheses -Wno-write-strings
CFLAGS += -fPIC -g -O1 -Wall
CORE = CallBack CompatClient CompatListener CompatServer Config DSO Device DeviceAddress DeviceDSO DeviceInfo DeviceLibrary DeviceMonitor Enumerable FAMonitor FormatDSO FormatLibrary FormatRaw Fsd FSDTab Inventory IOHandler Log MediaDaemon MonitorClient ParallelAddress Partition PartitionAddress RPCListener_linux SCSIAddress Scheduler SimpleVolume Task UnknownDevice Volume VolumeAddress bool main strplus DSReq_linux linux_platform
OBJECTS = $(CORE:%=build/%.o) build/xdr_mc.o
all: build/mediad-linux build/libmediad.a build/dev_linux.so build/fmt_iso.so build/fmt_udf.so build/libmediaclient.so build/libmediaclient.a build/media-client
build/mediad-linux: $(OBJECTS)
	$(CXX) -rdynamic $^ ../irix-device-ecosystem/build/libirixdev.a -ltirpc -ldl -o $@
build/libmediad.a: build/libmediad.o
	$(AR) rcs $@ $^
build/%.o: src/%.C
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
build/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
clean:
	rm -f build/*.o build/*.d build/mediad-linux build/libmediad.a
-include $(wildcard build/*.d)
.PHONY: all clean

build/dev_linux.so: build/dev_linux.o ../irix-device-ecosystem/build/libirixdev.a
	$(CXX) -shared $^ -o $@
build/fmt_iso.so: build/fmt_iso.o
	$(CXX) -shared $^ -o $@
build/fmt_udf.so: build/fmt_udf.o
	$(CXX) -shared $^ -o $@

build/libmediaclient.so: build/mediaclient.o build/xdr_mc.o
	$(CC) -shared -Wl,-soname,libmediaclient.so $^ -ltirpc -o $@
build/libmediaclient.a: build/mediaclient.o build/xdr_mc.o
	$(AR) rcs $@ $^
build/media-client: src/media-client.c build/libmediaclient.so
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -Lbuild -lmediaclient -ltirpc -Wl,-rpath,'$$ORIGIN' -o $@
