CXX ?= c++
PKG_CONFIG ?= pkg-config
DESTDIR ?=
PREFIX ?= /usr
PCSCLITE_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpcsclite 2>/dev/null)
PCSCLITE_LIBS := $(shell $(PKG_CONFIG) --libs libpcsclite 2>/dev/null)
PCSCLITE_HP_DROPDIR ?= $(shell $(PKG_CONFIG) --variable=usbdropdir libpcsclite 2>/dev/null)
PCSCLITE_HP_DROPDIR := $(if $(PCSCLITE_HP_DROPDIR),$(PCSCLITE_HP_DROPDIR),$(PREFIX)/lib/pcsc/drivers)

CPPFLAGS += -Iinclude -I../include -I../winusb/src/DriverHost_PX4 $(PCSCLITE_CFLAGS)
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -fPIC -Wall -Wextra -Wpedantic
LDLIBS += $(PCSCLITE_LIBS) -pthread

BUILD_DIR := build
SMART_CARD_SRC := ../winusb/src/DriverHost_PX4/smart_card.cpp
LIBRARY := $(BUILD_DIR)/libifd-px4.so
LIBRARY_OBJECTS := $(BUILD_DIR)/ifd_handler.o $(BUILD_DIR)/linux_card_device.o \
	$(BUILD_DIR)/smart_card.o
TEST := $(BUILD_DIR)/smart_card_state_test
BUNDLE_DIR := ifd-px4.bundle/Contents/Linux

.PHONY: all clean install test

all: $(LIBRARY) $(BUNDLE_DIR)/libifd-px4.so

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/ifd_handler.o: src/ifd_handler.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/linux_card_device.o: src/linux_card_device.cpp src/linux_card_device.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/smart_card.o: $(SMART_CARD_SRC) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(LIBRARY): $(LIBRARY_OBJECTS)
	$(CXX) -shared $^ $(LDLIBS) -o $@

$(BUNDLE_DIR)/libifd-px4.so: $(LIBRARY)
	mkdir -p $(BUNDLE_DIR)
	cp $(LIBRARY) $@

$(TEST): ../winusb/tests/smart_card_state_test.cpp $(SMART_CARD_SRC) | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -pthread -o $@

test: $(TEST)
	./$(TEST)

install: all
	install -d "$(DESTDIR)$(PCSCLITE_HP_DROPDIR)/ifd-px4.bundle/Contents/Linux"
	install -m 644 ifd-px4.bundle/Contents/Info.plist \
		"$(DESTDIR)$(PCSCLITE_HP_DROPDIR)/ifd-px4.bundle/Contents/Info.plist"
	install -m 755 $(LIBRARY) \
		"$(DESTDIR)$(PCSCLITE_HP_DROPDIR)/ifd-px4.bundle/Contents/Linux/libifd-px4.so"

clean:
	rm -rf $(BUILD_DIR) $(BUNDLE_DIR)
