cmake_minimum_required(VERSION 3.16)

project(sfd_tool LANGUAGES CXX)

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

# Termux 支持：与 Makefile 的 termux 目标对应
option(TERMUX "Enable Termux specific build options" OFF)

# 编译类型对应 make debug/release
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ================= 子目录模块 =================
add_subdirectory(core)
add_subdirectory(pages)

# ================= 主可执行程序 =================
add_executable(sfd_tool
    main.cpp
    main_console.cpp
    common.cpp
    GtkWidgetHelper.cpp
    ui_common.cpp
)

# 链接子模块
target_link_libraries(sfd_tool
    PRIVATE
        core_lib
        pages_lib
)

# 公共目标列表，方便批量设置编译选项
set(SFD_COMMON_TARGETS sfd_tool core_lib pages_lib)

# 头文件路径
target_include_directories(sfd_tool
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${CMAKE_CURRENT_SOURCE_DIR}/pages
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

# ================= 依赖：GTK3 / libusb =================
find_package(PkgConfig QUIET)

# --- GTK3 ---
if (USE_GTK)
    if (WIN32)
        # Windows: headers/libraries are provided by MSYS2 GTK in the CI image.
        # Include path differs for x64/x86 (mingw64 vs mingw32).
        if (CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
            # x64: use mingw64
            foreach(tgt IN LISTS SFD_COMMON_TARGETS)
                target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
                target_include_directories(${tgt} PRIVATE
                    "C:/msys64/mingw64/include/gtk-3.0"
                    "C:/msys64/mingw64/include/glib-2.0"
                    "C:/msys64/mingw64/lib/glib-2.0/include"
                    "C:/msys64/mingw64/include/pango-1.0"
                    "C:/msys64/mingw64/include/cairo"
                    "C:/msys64/mingw64/include/gdk-pixbuf-2.0"
                    "C:/msys64/mingw64/include/atk-1.0"
                    "C:/msys64/mingw64/include/harfbuzz"
                )
            endforeach()
        else()
            # Win32: use mingw32
            foreach(tgt IN LISTS SFD_COMMON_TARGETS)
                target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
                target_include_directories(${tgt} PRIVATE
                    "C:/msys64/mingw32/include/gtk-3.0"
                    "C:/msys64/mingw32/include/glib-2.0"
                    "C:/msys64/mingw32/lib/glib-2.0/include"
                    "C:/msys64/mingw32/include/pango-1.0"
                    "C:/msys64/mingw32/include/cairo"
                    "C:/msys64/mingw32/include/gdk-pixbuf-2.0"
                    "C:/msys64/mingw32/include/atk-1.0"
                    "C:/msys64/mingw32/include/harfbuzz"
                )
            endforeach()
        endif()
    else()
        if (NOT PKG_CONFIG_FOUND)
            message(FATAL_ERROR "PkgConfig is required when USE_GTK=ON")
        endif()

        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
            target_include_directories(${tgt} PRIVATE ${GTK3_INCLUDE_DIRS})
            target_link_directories(${tgt} PRIVATE ${GTK3_LIBRARY_DIRS})
        endforeach()

        # 只在最终可执行文件上链接 GTK 库
        target_link_libraries(sfd_tool PRIVATE ${GTK3_LIBRARIES})
    endif()
else()
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE USE_GTK=0)
    endforeach()
endif()

# --- libusb ---
if (USE_LIBUSB)
    if (WIN32)
        # Windows 下 libusb 由 third_party/Lib 提供，链接在 CI 中补充库目录
        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=1)
            target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Lib)
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

        # 同样只在最终可执行文件上链接 libusb 库
        target_link_libraries(sfd_tool PRIVATE ${LIBUSB_LIBRARIES})
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
