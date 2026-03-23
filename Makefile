# 配置选项
LIBUSB = 1
GTK = 1

# 检测操作系统
UNAME_S := $(shell uname -s)

# ================= CMake 驱动构建（保持与 scripts/dev.sh 一致） =================
# 说明：
# - 直接使用 make 时，先用 CMake 生成构建目录和 version.h 等中间文件，再调用底层构建系统（Ninja/Unix Makefiles）。
# - 这样可以复用 CMakeLists.txt 中的依赖检测、版本号生成、locale/icon 规则等，避免与纯手写 Makefile 行为不一致。

# 默认构建目录
BUILD_DIR ?= build_cmake_make

# 默认生成器（与 scripts/dev.sh 一致：优先 Ninja）
# 可以在命令行通过 `make GENERATOR="Unix Makefiles"` 覆盖
GENERATOR ?= $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

# 默认目标：通过 CMake 构建可执行文件和相关资源
.PHONY: all
all:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(BUILD_DIR) -j

# 调试构建
.PHONY: debug
debug:
	cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	cmake --build $(BUILD_DIR) -j

# 清理生成目录
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

# 兼容旧的 install/uninstall/check-deps/help 目标：
# 这些仍沿用原有 Makefile 的行为，避免破坏外部脚本依赖。

# 基础编译选项
CXXFLAGS = -O2 -Wall -Wextra -std=c++17 -pedantic -Wno-unused
CXXFLAGS += -DUSE_LIBUSB=$(LIBUSB)
CXXFLAGS += -DUSE_GTK=$(GTK)

LIBS = -lm -lpthread
APPNAME = sfd_tool

# 操作系统特定的设置
ifeq ($(UNAME_S), Darwin)  # macOS
    # macOS 特定配置
    CXXFLAGS += -DMACOS
    # macOS 上的 libusb 包名
    LIBUSB_PKG = libusb-1.0
    # macOS 上可能需要 Homebrew 安装的 GTK
    GTK_PKG = gtk+-3.0
    # macOS 上的编译器通常是 clang++
    CXX = clang++
else ifeq ($(UNAME_S), Linux)  # Linux
    # Linux 特定配置
    CXXFLAGS += -DLINUX
    LIBUSB_PKG = libusb-1.0
    GTK_PKG = gtk+-3.0
    CXX = g++
else
    $(error Unsupported operating system: $(UNAME_S))
endif

# 条件编译设置
ifeq ($(LIBUSB), 1)
    CXXFLAGS += -DUSE_LIBUSB=1
    LIBS += `pkg-config --libs $(LIBUSB_PKG) 2>/dev/null || echo "-lusb-1.0"`
endif

ifeq ($(GTK), 1)
    # 检查 pkg-config 是否存在
    PKG_CONFIG_EXISTS := $(shell pkg-config --exists $(GTK_PKG) && echo 1 || echo 0)

    ifeq ($(PKG_CONFIG_EXISTS), 1)
        CXXFLAGS += `pkg-config --cflags $(GTK_PKG)`
        LIBS += `pkg-config --libs $(GTK_PKG)`
    else
        # pkg-config 不可用时的备选方案
        ifeq ($(UNAME_S), Darwin)
            # macOS 上 GTK 可能通过 Homebrew 安装在其他位置
            HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo "/usr/local")
            CXXFLAGS += -I$(HOMEBREW_PREFIX)/include/gtk-3.0 \
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
            CXXFLAGS += $(shell pkg-config --cflags gtk+-3.0 2>/dev/null || echo "")
            LIBS += $(shell pkg-config --libs gtk+-3.0 2>/dev/null || echo "-lgtk-3")
        endif
    endif
endif

# 添加框架 (macOS 需要)
ifeq ($(UNAME_S), Darwin)
    # macOS 上需要链接 CoreFoundation 框架
    LIBS += -framework CoreFoundation -framework IOKit
endif

# 安装目录定义
BINDIR ?= $(PREFIX)/bin
DATADIR ?= $(PREFIX)/share
APPDIR ?= $(DATADIR)/applications
ICONDIR ?= $(DATADIR)/icons/hicolor
DOCDIR ?= $(DATADIR)/doc/sfd-tool
LOCALEDIR ?= $(DATADIR)/locale

# CXXFLAGS += $(shell pkg-config --cflags nlohmann_json 2>/dev/null || echo "-I/usr/include/nlohmann")
# LIBS += $(shell pkg-config --libs nlohmann_json 2>/dev/null || echo "-ljson")

# locale 生成（仍用于 install 兼容逻辑）
.PHONY: update-po
update-po:
	@if command -v xgettext >/dev/null 2>&1; then \
	  echo "[i18n] Updating locale/sfd_tool.pot ..."; \
	  xgettext --language=C++ --keyword=_ --from-code=UTF-8 \
	    --output=locale/sfd_tool.pot main.cpp GtkWidgetHelper.cpp pages/page_*.cpp ui_common.cpp; \
	  if command -v python3 >/dev/null 2>&1; then \
	    python3 scripts/gen_po.py; \
	  elif command -v python >/dev/null 2>&1; then \
	    python scripts/gen_po.py; \
	  else \
	    echo "[i18n] Warning: python3/python not found; skip gen_po.py" >&2; \
	  fi; \
	else \
	  echo "[i18n] Warning: xgettext not found; skip POT/PO update" >&2; \
	fi

.PHONY: locales
locales: update-po locale/zh_CN/LC_MESSAGES/sfd_tool.mo

locale/zh_CN/LC_MESSAGES/sfd_tool.mo: locale/zh_CN/LC_MESSAGES/sfd_tool.po
	msgfmt $< -o $@

# 安装目标（通过 CMake install 收敛生命周期）
.PHONY: install
install: all
	# 使用 CMake 安装，复用 CMakeLists.txt 中的安装规则
	cmake --install $(BUILD_DIR) --prefix $(DESTDIR)$(PREFIX)

# 卸载目标
.PHONY: uninstall
uninstall:
	# CMake 本身没有通用卸载逻辑，这里保留简单的清理示例；
	# 如需更精确的卸载建议使用发行版包管理器或手工删除。
	rm -f $(DESTDIR)$(BINDIR)/$(APPNAME)
	rm -f $(DESTDIR)$(APPDIR)/sfd-tool.desktop
	rm -rf $(DESTDIR)$(DOCDIR)
	rm -f $(DESTDIR)$(LOCALEDIR)/zh_CN/LC_MESSAGES/sfd_tool.mo
	find $(DESTDIR)$(ICONDIR) -name "sfd-tool.png" -delete

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
#   @pkg-config --exists nlohmann_json && echo "nlohmann_json: Found" || echo "nlohmann_json: Not found"

	@echo "Compiler: $(CXX)"

# 帮助信息
.PHONY: help
help:
	@echo "Available targets:"
	@echo "  all       - Configure & build via CMake (default)"
	@echo "  debug     - Debug build via CMake (CMAKE_BUILD_TYPE=Debug)"
	@echo "  clean     - Remove CMake build directory ($(BUILD_DIR))"
	@echo "  install   - Install to /usr/local/bin (legacy target, still available)"
	@echo "  uninstall - Uninstall from /usr/local/bin"
	@echo "  check-deps - Check required dependencies"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Notes:"
	@echo "  - Direct 'make' now uses CMake under the hood, consistent with ./scripts/dev.sh."
	@echo "  - Use BUILD_DIR=/path/to/build to customize the CMake build directory."
	@echo "  - Use GENERATOR=\"Unix Makefiles\" if Ninja is not desired."
