# 配置选项
LIBUSB = 1
GTK = 1

# 基础编译选项
CFLAGS = -O2 -Wall -Wextra -std=c++14 -pedantic -Wno-unused
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)
CFLAGS += -DUSE_GTK=$(GTK)

LIBS = -lm -lpthread
CC = g++
APPNAME = sfd_tool

# 条件编译设置
ifeq ($(LIBUSB), 1)
CFLAGS += -DUSE_LIBUSB=1
LIBS += -lusb-1.0
endif

ifeq ($(GTK), 1)
# GTK3 编译选项 (Linux)
CFLAGS += `pkg-config --cflags gtk+-3.0`
LIBS += `pkg-config --libs gtk+-3.0`
endif

# 默认目标
.PHONY: all
all: $(APPNAME)

# 主目标
$(APPNAME): main.cpp common.cpp
	$(CC) -s $(CFLAGS) -o $@ $^ $(LIBS)

# 清理目标
.PHONY: clean
clean:
	rm -f $(APPNAME) *.o