
LIBUSB = 1
CFLAGS = -O2 -Wall -Wextra -std=c++14 -pedantic -Wno-unused
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)
LIBS = -lm -lpthread
CC = g++
APPNAME = sfd_tool

LIBUSB = 1
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)

ifeq ($(LIBUSB), 1)
LIBS += -lusb-1.0
endif

#make
$(APPNAME): main.cpp common.cpp
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)
