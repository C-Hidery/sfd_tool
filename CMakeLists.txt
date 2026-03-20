cmake_minimum_required(VERSION 3.16)

project(sfd_tool LANGUAGES CXX)

# ================= 版本号（从 VERSION 文件生成） =================
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION.txt" SFD_TOOL_VERSION_STR LIMIT_COUNT 1)
string(STRIP "${SFD_TOOL_VERSION_STR}" SFD_TOOL_VERSION_STR)
message(STATUS "SFD Tool version: ${SFD_TOOL_VERSION_STR}")

configure_file(
    "${CMAKE_SOURCE_DIR}/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

# ================= 通用编译配置 =================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 对标 Makefile: -Wall -Wextra -pedantic -Wno-unused
if (MSVC)
    # Windows 下使用 MSVC，保持告警级别，但不强行添加 GCC/Clang 专用选项
else()
    add_compile_options(-Wall -Wextra -pedantic -Wno-unused)
endif()

# 对标 Makefile: LIBUSB / GTK 开关
option(USE_LIBUSB "Enable libusb backend" ON)
option(USE_GTK "Enable GTK3 UI" ON)

# Windows x86 (Win32) 不支持 libusb：强制关闭，统一走 Channel9 封装
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND USE_LIBUSB)
    message(STATUS "USE_LIBUSB=ON is not supported on Win32 (x86); forcing USE_LIBUSB=OFF (Channel9 backend)")
    set(USE_LIBUSB OFF CACHE BOOL "Enable libusb backend" FORCE)
endif()


# Termux 支持：与 Makefile 的 termux 目标对应
option(TERMUX "Enable Termux specific build options" OFF)

# 编译类型对应 make debug/release
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ================= 子目录模块 =================
add_subdirectory(core)
add_subdirectory(pages)

option(SFD_ENABLE_TESTS "Enable unit tests (ctest)" OFF)
if (SFD_ENABLE_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

# ================= 公共 UI / CLI 静态库（主程序和测试共用） =================
add_library(sfd_ui STATIC
    common.cpp
    GtkWidgetHelper.cpp
    ui_common.cpp
)

target_link_libraries(sfd_ui
    PUBLIC
        pages_lib
        core_lib
)

target_include_directories(sfd_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/pages
        ${CMAKE_BINARY_DIR}/generated
)

# ================= 主可执行程序 =================
add_executable(sfd_tool
    main.cpp
    main_console.cpp
)

# Windows: Channel9 封装（x86 使用，不依赖 libusb）
if (WIN32)
    target_sources(sfd_tool PRIVATE
        BMPlatform.cpp
        Wrapper.cpp
    )
endif()

# 链接子模块和公共 UI 库
target_link_libraries(sfd_tool
    PRIVATE
        sfd_ui
)

# 公共目标列表，方便批量设置编译选项
set(SFD_COMMON_TARGETS sfd_tool core_lib pages_lib sfd_ui ${SFD_TEST_TARGETS})

# 头文件路径
target_include_directories(sfd_tool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/pages
        ${CMAKE_BINARY_DIR}/generated
)

# ================= 平台宏 & 基础库 =================
if (APPLE)
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE MACOS)
    endforeach()

    # 对标 Makefile 中的 CoreFoundation / IOKit
    target_link_libraries(sfd_tool PRIVATE
        "-framework CoreFoundation"
        "-framework IOKit"
    )
elseif (WIN32)
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE WINDOWS)
    endforeach()

    # Windows 下不需要显式链接 libm，C 运行库已包含
elseif (UNIX)
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE LINUX)
    endforeach()
endif()

# Termux 额外链接选项（与 Makefile 的 termux 目标对应）
if (TERMUX)
    target_compile_definitions(sfd_tool PRIVATE TERMUX)
    target_link_libraries(sfd_tool PRIVATE execinfo)
endif()

# 对标 LIBS = -lm -lpthread
find_package(Threads REQUIRED)
# m 库仅在类 Unix 平台需要；Windows 使用 MSVC 运行库
if (NOT WIN32)
    target_link_libraries(sfd_tool PRIVATE Threads::Threads m)
else()
    target_link_libraries(sfd_tool PRIVATE Threads::Threads)
endif()

# Windows: libintl (gettext) via vcpkg
if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_vcpkg_triplet "x64-windows")
    else()
        # 32-bit (Win32) build
        set(_vcpkg_triplet "x86-windows")
    endif()

    # Try to infer vcpkg root from toolchain file first, then env, then default C:/vcpkg
    set(_vcpkg_root "")

    if (DEFINED CMAKE_TOOLCHAIN_FILE)
        get_filename_component(_tc_dir "${CMAKE_TOOLCHAIN_FILE}" DIRECTORY)
        get_filename_component(_candidate_root "${_tc_dir}/../.." ABSOLUTE)
        if (EXISTS "${_candidate_root}/installed")
            set(_vcpkg_root "${_candidate_root}")
        endif()
    endif()

    if (_vcpkg_root STREQUAL "" AND DEFINED ENV{VCPKG_ROOT})
        set(_vcpkg_root "$ENV{VCPKG_ROOT}")
    endif()

    if (_vcpkg_root STREQUAL "")
        set(_vcpkg_root "C:/vcpkg")
    endif()

    set(_vcpkg_include "${_vcpkg_root}/installed/${_vcpkg_triplet}/include")
    set(_vcpkg_lib_dir "${_vcpkg_root}/installed/${_vcpkg_triplet}/lib")

    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_include_directories(${tgt} PRIVATE "${_vcpkg_include}")
    endforeach()

    find_library(INTL_LIBRARY NAMES intl PATHS "${_vcpkg_lib_dir}")
    if (INTL_LIBRARY)
        target_link_libraries(sfd_tool PRIVATE "${INTL_LIBRARY}")
    else()
        message(WARNING "intl library not found in ${_vcpkg_lib_dir}; gettext may fail to link")
    endif()

    # Windows 平台：崩溃堆栈 (dbghelp) + 串口枚举 (setupapi)
    target_link_libraries(sfd_tool PRIVATE dbghelp setupapi)
endif()

# ================= 依赖：GTK3 / libusb =================
find_package(PkgConfig QUIET)

# --- GTK3 ---
if (USE_GTK)
    if (WIN32)
        # Windows: headers/libraries are provided by MSYS2 GTK in the CI image.
        # Use mingw64 for 64-bit builds and mingw32 for 32-bit (x86) builds.
        if (CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_gtk_root "C:/msys64/mingw64")
        else()
            set(_gtk_root "C:/msys64/mingw32")
        endif()

        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
            target_include_directories(${tgt} PRIVATE
                "${_gtk_root}/include/gtk-3.0"
                "${_gtk_root}/include/glib-2.0"
                "${_gtk_root}/lib/glib-2.0/include"
                "${_gtk_root}/include/pango-1.0"
                "${_gtk_root}/include/cairo"
                "${_gtk_root}/include/gdk-pixbuf-2.0"
                "${_gtk_root}/include/atk-1.0"
                "${_gtk_root}/include/harfbuzz"
            )
        endforeach()

        # Link against GTK/glib import libraries from MSYS2
        set(_gtk_win_libs
            "${_gtk_root}/lib/libgtk-3.dll.a"
            "${_gtk_root}/lib/libgdk-3.dll.a"
            "${_gtk_root}/lib/libglib-2.0.dll.a"
            "${_gtk_root}/lib/libgobject-2.0.dll.a"
            "${_gtk_root}/lib/libgio-2.0.dll.a"
            "${_gtk_root}/lib/libharfbuzz.dll.a"
            "${_gtk_root}/lib/libpango-1.0.dll.a"
            "${_gtk_root}/lib/libpangocairo-1.0.dll.a"
        )

        target_link_libraries(sfd_tool PRIVATE ${_gtk_win_libs})
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_libraries(${tgt} PRIVATE ${_gtk_win_libs})
        endforeach()
    else()
        if (NOT PKG_CONFIG_FOUND)
            message(FATAL_ERROR "PkgConfig is required when USE_GTK=ON")
        endif()

        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

        # Fedora 等发行版在 RPM 构建环境中会默认启用 GNU ld 的 --as-needed/
        # --no-copy-dt-needed-entries，这在 GTK 这类有复杂依赖链的库上容易触发
        # “DSO missing from command line” 报错。这里为主可执行文件放宽该限制，
        # 确保所有被依赖的 DSO 都会被保留。
        if (UNIX AND NOT APPLE)
            target_link_options(sfd_tool PRIVATE "-Wl,--copy-dt-needed-entries")
        endif()

        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
            target_include_directories(${tgt} PRIVATE ${GTK3_INCLUDE_DIRS})
            target_link_directories(${tgt} PRIVATE ${GTK3_LIBRARY_DIRS})
        endforeach()

        # 在最终可执行文件和测试可执行文件上链接 GTK 库
        target_link_libraries(sfd_tool PRIVATE ${GTK3_LIBRARIES})
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_libraries(${tgt} PRIVATE ${GTK3_LIBRARIES})
        endforeach()

        # Work around GNU ld "DSO missing from command line" on some distros
        # by explicitly appending pango-1.0 at the end of the link line.
        # This ensures -lpango-1.0 appears after all static archives.
        find_library(PANGO_LIB NAMES pango-1.0)
        if (PANGO_LIB)
            target_link_libraries(sfd_tool PRIVATE ${PANGO_LIB})
            foreach(tgt IN LISTS SFD_TEST_TARGETS)
                target_link_libraries(${tgt} PRIVATE ${PANGO_LIB})
            endforeach()
        endif()
    endif()
else()
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE USE_GTK=0)
    endforeach()
endif()

# --- libusb ---
if (USE_LIBUSB)
    if (WIN32)
        # Windows 下 libusb 由 third_party/Lib 提供
        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=1)
            target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Lib)
        endforeach()

        # 链接本地提供的 libusb-1.0.lib
        set(_libusb_win_lib "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Lib/libusb-1.0.lib")

        target_link_libraries(sfd_tool PRIVATE ${_libusb_win_lib})
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_libraries(${tgt} PRIVATE ${_libusb_win_lib})
        endforeach()
    else()
        if (NOT PKG_CONFIG_FOUND)
            message(FATAL_ERROR "PkgConfig is required when USE_LIBUSB=ON")
        endif()

        pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=1)
            target_include_directories(${tgt} PRIVATE ${LIBUSB_INCLUDE_DIRS})
            target_link_directories(${tgt} PRIVATE ${LIBUSB_LIBRARY_DIRS})
        endforeach()

        # 在最终可执行文件和测试可执行文件上链接 libusb 库
        target_link_libraries(sfd_tool PRIVATE ${LIBUSB_LIBRARIES})
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_libraries(${tgt} PRIVATE ${LIBUSB_LIBRARIES})
        endforeach()
    endif()
else()
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=0)
    endforeach()
endif()

# ================= locales 生成 =================
# 对标 Makefile: locale/zh_CN/LC_MESSAGES/sfd_tool.mo 由 .po 生成
find_program(MSGFMT_EXECUTABLE msgfmt)
if (NOT MSGFMT_EXECUTABLE)
    message(WARNING "msgfmt not found; locale .mo will not be built")
else()
    set(LOCALE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/locale/zh_CN/LC_MESSAGES)
    set(SFD_PO ${LOCALE_DIR}/sfd_tool.po)
    set(SFD_MO ${CMAKE_BINARY_DIR}/locale/zh_CN/LC_MESSAGES/sfd_tool.mo)

    add_custom_command(
        OUTPUT ${SFD_MO}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/locale/zh_CN/LC_MESSAGES
        COMMAND ${MSGFMT_EXECUTABLE} ${SFD_PO} -o ${SFD_MO}
        DEPENDS ${SFD_PO}
        COMMENT "Generating sfd_tool.mo from .po"
        VERBATIM
    )

    add_custom_target(locales ALL
        DEPENDS ${SFD_MO}
    )
endif()

# ================= 安装规则 =================
include(GNUInstallDirs)

# 为运行时 About 页面提供已安装文档目录（用于查找 VERSION_LOG.md）
# 使用 CMAKE_INSTALL_FULL_DATADIR，确保包含安装前缀（例如 /usr/share）
add_compile_definitions(SFD_TOOL_DOC_DIR="${CMAKE_INSTALL_FULL_DATADIR}/doc/sfd-tool")

# 对标 Makefile 中的变量
# BINDIR -> CMAKE_INSTALL_BINDIR
# DATADIR -> CMAKE_INSTALL_DATADIR
# APPDIR -> share/applications
# ICONDIR -> share/icons/hicolor
# DOCDIR -> share/doc/sfd-tool
# LOCALEDIR -> share/locale

# 安装二进制
install(TARGETS sfd_tool
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 安装文档
install(FILES
    LICENSE.txt
    README.md
    README_ZH.md
    docs/VERSION_LOG.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sfd-tool
)

# 安装 desktop 文件（替换 Icon 名称与 Makefile 一致）
set(DESKTOP_IN  ${CMAKE_CURRENT_SOURCE_DIR}/packaging/sfd_tool.desktop)
set(DESKTOP_OUT ${CMAKE_CURRENT_BINARY_DIR}/sfd-tool.desktop)

if (EXISTS ${DESKTOP_IN})
    file(READ ${DESKTOP_IN} DESKTOP_CONTENT)
    string(REPLACE "Icon=sfd_tool" "Icon=sfd-tool" DESKTOP_CONTENT "${DESKTOP_CONTENT}")
    file(WRITE ${DESKTOP_OUT} "${DESKTOP_CONTENT}")

    install(FILES ${DESKTOP_OUT}
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ
    )
endif()

# 安装图标（需要 ImageMagick convert，行为与 Makefile 类似；Windows 上跳过）
if (NOT WIN32)
    find_program(CONVERT_EXECUTABLE convert)
    set(ICON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png)

    if (EXISTS ${ICON_SRC} AND CONVERT_EXECUTABLE)
        set(ICON_SIZES 16 32 48 256)
        set(GENERATED_ICONS)

        foreach(size IN LISTS ICON_SIZES)
            set(OUT_ICON ${CMAKE_CURRENT_BINARY_DIR}/sfd-tool-${size}.png)
            add_custom_command(
                OUTPUT ${OUT_ICON}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}
                COMMAND ${CONVERT_EXECUTABLE} ${ICON_SRC} -resize ${size}x${size} ${OUT_ICON}
                DEPENDS ${ICON_SRC}
                COMMENT "Generating icon ${size}x${size}"
                VERBATIM
            )
            list(APPEND GENERATED_ICONS ${OUT_ICON})

            install(FILES ${OUT_ICON}
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME sfd-tool.png
            )
        endforeach()

        add_custom_target(icons ALL
            DEPENDS ${GENERATED_ICONS}
        )
    endif()
endif()

# 安装 locale .mo（如果已成功生成）
if (MSGFMT_EXECUTABLE)
    install(FILES ${CMAKE_BINARY_DIR}/locale/zh_CN/LC_MESSAGES/sfd_tool.mo
        DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/zh_CN/LC_MESSAGES
    )
endif()

# 可选：安装 man 手册页
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man_sfd-tool.1)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man_sfd-tool.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
        RENAME sfd-tool.1
    )
endif()

# 可选：gtk-update-icon-cache，保留与 Makefile 类似的行为
find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
if (GTK_UPDATE_ICON_CACHE)
    add_custom_target(update-icon-cache
        COMMAND ${GTK_UPDATE_ICON_CACHE} -q -t -f ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        COMMENT "Updating GTK icon cache"
    )
endif()
