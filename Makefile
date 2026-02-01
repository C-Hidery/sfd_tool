# 配置选项
LIBUSB = 1
GTK = 1

# 检测操作系统
UNAME_S := $(shell uname -s)

# 基础编译选项
CFLAGS = -O2 -Wall -Wextra -std=c++14 -pedantic -Wno-unused
CFLAGS += -DUSE_LIBUSB=$(LIBUSB)
CFLAGS += -DUSE_GTK=$(GTK)

LIBS = -lm -lpthread
CC = g++
APPNAME = sfd_tool

# 操作系统特定的设置
ifeq ($(UNAME_S), Darwin)  # macOS
    # macOS 特定配置
    CFLAGS += -DMACOS
    # macOS 上的 libusb 包名
    LIBUSB_PKG = libusb-1.0
    # macOS 上可能需要 Homebrew 安装的 GTK
    GTK_PKG = gtk+-3.0
    # macOS 上的编译器通常是 clang++
    CC = clang++
else ifeq ($(UNAME_S), Linux)  # Linux
    # Linux 特定配置
    CFLAGS += -DLINUX
    LIBUSB_PKG = libusb-1.0
    GTK_PKG = gtk+-3.0
else
    $(error Unsupported operating system: $(UNAME_S))
endif

# 条件编译设置
ifeq ($(LIBUSB), 1)
    CFLAGS += -DUSE_LIBUSB=1
    LIBS += `pkg-config --libs $(LIBUSB_PKG) 2>/dev/null || echo "-lusb-1.0"`
endif

ifeq ($(GTK), 1)
    # 检查 pkg-config 是否存在
    PKG_CONFIG_EXISTS := $(shell pkg-config --exists $(GTK_PKG) && echo 1 || echo 0)
    
    ifeq ($(PKG_CONFIG_EXISTS), 1)
        CFLAGS += `pkg-config --cflags $(GTK_PKG)`
        LIBS += `pkg-config --libs $(GTK_PKG)`
    else
        # pkg-config 不可用时的备选方案
        ifeq ($(UNAME_S), Darwin)
            # macOS 上 GTK 可能通过 Homebrew 安装在其他位置
            HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo "/usr/local")
            CFLAGS += -I$(HOMEBREW_PREFIX)/include/gtk-3.0 \
                      -I$(HOMEBREW_PREFIX)/include/glib-2.0 \
                      -I$(HOMEBREW_PREFIX)/lib/glib-2.0/include \
                      -I$(HOMEBREW_PREFIX)/include/pango-1.0 \
                      -I$(HOMEBREW_PREFIX)/include/cairo \
                      -I$(HOMEBREW_PREFIX)/include/gdk-pixbuf-2.0 \
                      -I$(HOMEBREW_PREFIX)/include/atk-1.0
            LIBS += -L$(HOMEBREW_PREFIX)/lib -lgtk-3 -lgdk-3 -lpangocairo-1.0 -lpango-1.0 \
                    -lharfbuzz -latk-1.0 -lcairo-gobject -lcairo -lgdk_pixbuf-2.0 \
                    -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lintl
        else
            # Linux 上的备选方案
            CFLAGS += $(shell pkg-config --cflags gtk+-3.0 2>/dev/null || echo "")
            LIBS += $(shell pkg-config --libs gtk+-3.0 2>/dev/null || echo "-lgtk-3")
        endif
    endif
endif

# 添加框架 (macOS 需要)
ifeq ($(UNAME_S), Darwin)
    # macOS 上需要链接 CoreFoundation 框架
    LIBS += -framework CoreFoundation -framework IOKit
endif

# 默认目标
.PHONY: all
all: $(APPNAME)

# 主目标
$(APPNAME): main.cpp common.cpp
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# 调试版本
.PHONY: debug
debug: CFLAGS += -g -DDEBUG -O0
debug: $(APPNAME)

# 发布版本
.PHONY: release
release: CFLAGS += -O3 -DNDEBUG
release: $(APPNAME)

# 静态分析
.PHONY: analyze
analyze: CFLAGS += -fanalyzer
analyze: debug

# 清理目标
.PHONY: clean
clean:
	rm -f $(APPNAME) *.o *.dSYM

# 安装目标
.PHONY: install
install: $(APPNAME)
ifeq ($(UNAME_S), Darwin)
	install -m 755 $(APPNAME) /usr/local/bin/
else
	install -m 755 $(APPNAME) /usr/local/bin/
endif

# 卸载目标
.PHONY: uninstall
uninstall:
	rm -f /usr/local/bin/$(APPNAME)

# 依赖检查
.PHONY: check-deps
check-deps:
	@echo "Checking dependencies..."
	@echo "OS: $(UNAME_S)"
ifeq ($(GTK),1)
	@pkg-config --exists $(GTK_PKG) && echo "GTK3: Found" || echo "GTK3: Not found"
endif
ifeq ($(LIBUSB),1)
	@pkg-config --exists $(LIBUSB_PKG) && echo "libusb: Found" || echo "libusb: Not found"
endif
	@echo "Compiler: $(CC)"

# 帮助信息
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all       - Build the application (default)"
	@echo "  debug     - Build with debug symbols"
	@echo "  release   - Build with optimization"
	@echo "  clean     - Remove build artifacts"
	@echo "  install   - Install to /usr/local/bin"
	@echo "  uninstall - Uninstall from /usr/local/bin"
	@echo "  check-deps- Check required dependencies"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Configuration options:"
	@echo "  make GTK=0     - Build without GTK support"
	@echo "  make LIBUSB=0  - Build without libusb support"
	@echo "  make CC=clang++- Use clang++ compiler (Linux)"