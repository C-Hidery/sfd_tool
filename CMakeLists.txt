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

if (MSVC)
    # Windows 下使用 MSVC，保持告警级别
else()
    add_compile_options(-Wall -Wextra -pedantic -Wno-unused)
endif()

option(USE_LIBUSB "Enable libusb backend" ON)
option(USE_GTK "Enable GTK3 UI" ON)

# Windows x86 (Win32) 不支持 libusb
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND USE_LIBUSB)
    message(STATUS "USE_LIBUSB=ON is not supported on Win32 (x86); forcing USE_LIBUSB=OFF")
    set(USE_LIBUSB OFF CACHE BOOL "Enable libusb backend" FORCE)
endif()

option(TERMUX "Enable Termux specific build options" OFF)

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

# ================= 公共 UI 静态库 =================
add_library(sfd_ui STATIC
    common.cpp
    ui/GtkWidgetHelper.cpp
    ui/ui_common.cpp
    ui/layout/bottom_bar.cpp
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

# ================= Windows 资源文件 (.rc) 支持 =================
if (WIN32)
    set(RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/app.rc")
    if (EXISTS ${RESOURCE_FILE})
        target_sources(sfd_tool PRIVATE ${RESOURCE_FILE})
        set_source_files_properties(${RESOURCE_FILE} PROPERTIES LANGUAGE RC)
        message(STATUS "Including Windows resource file: ${RESOURCE_FILE}")
        target_compile_definitions(sfd_tool PRIVATE
            SFD_TOOL_VERSION="${SFD_TOOL_VERSION_STR}"
            SFD_TOOL_VERSION_COMMA=${SFD_TOOL_VERSION_STR}
        )
    else()
        message(WARNING "Resource file not found: ${RESOURCE_FILE}")
    endif()
endif()

# 链接子模块和公共 UI 库
if(APPLE OR WIN32)
    set(SFD_TOOL_LIBS core_lib pages_lib sfd_ui)
else()
    set(SFD_TOOL_LIBS core_lib pages_lib sfd_ui core_lib pages_lib sfd_ui)
endif()

target_link_libraries(sfd_tool PRIVATE ${SFD_TOOL_LIBS})

# Windows: 添加 BMPlatform/Wrapper 源文件（必须在 add_executable 之后）
if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT USE_LIBUSB)
        target_sources(sfd_tool PRIVATE BMPlatform.cpp Wrapper.cpp)
    elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(sfd_tool PRIVATE BMPlatform.cpp Wrapper.cpp)
    endif()
endif()

# 公共目标列表
set(SFD_COMMON_TARGETS sfd_tool core_lib pages_lib sfd_ui ${SFD_TEST_TARGETS})

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
    target_link_libraries(sfd_tool PRIVATE "-framework CoreFoundation" "-framework IOKit")
elseif (WIN32)
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE WINDOWS)
    endforeach()
elseif (UNIX)
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE LINUX)
    endforeach()
endif()

if (TERMUX)
    target_compile_definitions(sfd_tool PRIVATE TERMUX)
    target_link_libraries(sfd_tool PRIVATE execinfo)
endif()

find_package(Threads REQUIRED)
if (NOT WIN32)
    target_link_libraries(sfd_tool PRIVATE Threads::Threads m)
else()
    target_link_libraries(sfd_tool PRIVATE Threads::Threads)
endif()

# Windows: libintl (gettext) - 根据架构选择来源
if (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        # x64：优先使用 vcpkg 安装的 gettext
        set(_vcpkg_triplet "x64-windows")
        # 推断 vcpkg 根目录
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
        set(_vcpkg_lib_dir "${_vcpkg_root}/installed/${_vcpkg_triplet}/lib")
        
        find_library(INTL_LIBRARY NAMES intl PATHS "${_vcpkg_lib_dir}")
        if (INTL_LIBRARY)
            target_link_libraries(sfd_tool PRIVATE "${INTL_LIBRARY}")
            foreach(tgt IN LISTS SFD_TEST_TARGETS)
                target_link_libraries(${tgt} PRIVATE "${INTL_LIBRARY}")
            endforeach()
        else()
            message(WARNING "intl library not found in vcpkg path (${_vcpkg_lib_dir}); gettext may fail to link")
        endif()
    else()
        # x86：使用 MSYS2 的 gettext-runtime
        set(_gtk_root "C:/msys64/mingw32")
        find_library(INTL_LIBRARY NAMES intl libintl PATHS "${_gtk_root}/lib")
        if (INTL_LIBRARY)
            target_link_libraries(sfd_tool PRIVATE "${INTL_LIBRARY}")
            foreach(tgt IN LISTS SFD_TEST_TARGETS)
                target_link_libraries(${tgt} PRIVATE "${INTL_LIBRARY}")
            endforeach()
        else()
            message(WARNING "intl library not found in ${_gtk_root}/lib; gettext may fail to link")
        endif()
    endif()

    # 公共 Windows 库
    target_link_libraries(sfd_tool PRIVATE dbghelp setupapi)
    foreach(tgt IN LISTS SFD_TEST_TARGETS)
        target_link_libraries(${tgt} PRIVATE dbghelp setupapi)
    endforeach()
endif()
# 由于 x86 使用了 MinGW 编译的 libintl.a，MSVC 链接器需要禁用 SAFESEH
if (MSVC)
    target_link_options(sfd_tool PRIVATE /SAFESEH:NO)
    # 如果启用了测试，也为所有测试目标添加该选项
    if (SFD_ENABLE_TESTS)
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_options(${tgt} PRIVATE /SAFESEH:NO)
        endforeach()
    endif()
endif()
# ================= 依赖：GTK3 / libusb =================
find_package(PkgConfig QUIET)

# --- GTK3 ---
if (USE_GTK)
    if (WIN32)
        if (CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_gtk_root "C:/msys64/mingw64")
        else()
            set(_gtk_root "C:/msys64/mingw32")
        endif()

        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
            target_include_directories(${tgt} PRIVATE
                "${_gtk_root}/include"
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
        
        if (TARGET BMPlatform)
            target_link_libraries(BMPlatform PRIVATE ${_gtk_win_libs})
        endif()
    else()
        if (NOT PKG_CONFIG_FOUND)
            message(FATAL_ERROR "PkgConfig is required when USE_GTK=ON")
        endif()
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        if (UNIX AND NOT APPLE)
            target_link_options(sfd_tool PRIVATE "-Wl,--copy-dt-needed-entries")
        endif()
        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_GTK=1)
            target_include_directories(${tgt} PRIVATE ${GTK3_INCLUDE_DIRS})
            target_link_directories(${tgt} PRIVATE ${GTK3_LIBRARY_DIRS})
        endforeach()
        target_link_libraries(sfd_tool PRIVATE ${GTK3_LIBRARIES})
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_libraries(${tgt} PRIVATE ${GTK3_LIBRARIES})
        endforeach()
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
        foreach(tgt IN LISTS SFD_COMMON_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=1)
            target_include_directories(${tgt} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/Lib)
        endforeach()
        set(_libusb_win_lib "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Lib/libusb-1.0.lib")
        target_link_libraries(sfd_tool PRIVATE ${_libusb_win_lib})
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_link_libraries(${tgt} PRIVATE ${_libusb_win_lib})
        endforeach()
        if (TARGET BMPlatform)
            target_link_libraries(BMPlatform PRIVATE ${_libusb_win_lib})
        endif()
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
        target_link_libraries(core_lib PUBLIC ${LIBUSB_LIBRARIES})
    endif()
else()
    foreach(tgt IN LISTS SFD_COMMON_TARGETS)
        target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=0)
    endforeach()
endif()

# ================= locales 生成 =================
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
    add_custom_target(locales ALL DEPENDS ${SFD_MO})
endif()

# ================= 安装规则 =================
include(GNUInstallDirs)
add_compile_definitions(SFD_TOOL_DOC_DIR="${CMAKE_INSTALL_FULL_DATADIR}/doc/sfd-tool")

install(TARGETS sfd_tool RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if (TARGET BMPlatform)
    install(TARGETS BMPlatform RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if (TARGET Proxy32)
    install(TARGETS Proxy32 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(FILES LICENSE.txt README.md README_ZH.md docs/VERSION_LOG.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/sfd-tool)

set(DESKTOP_IN ${CMAKE_CURRENT_SOURCE_DIR}/packaging/sfd_tool.desktop)
set(DESKTOP_OUT ${CMAKE_CURRENT_BINARY_DIR}/sfd-tool.desktop)
if (EXISTS ${DESKTOP_IN})
    file(READ ${DESKTOP_IN} DESKTOP_CONTENT)
    string(REPLACE "Icon=sfd_tool" "Icon=sfd-tool" DESKTOP_CONTENT "${DESKTOP_CONTENT}")
    file(WRITE ${DESKTOP_OUT} "${DESKTOP_CONTENT}")
    install(FILES ${DESKTOP_OUT} DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
endif()

if (NOT WIN32)
    find_program(CONVERT_EXECUTABLE convert)
    set(ICON_SRC ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)
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
            install(FILES ${OUT_ICON} DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME sfd-tool.png)
        endforeach()
        add_custom_target(icons ALL DEPENDS ${GENERATED_ICONS})
    endif()
endif()

if (MSGFMT_EXECUTABLE)
    install(FILES ${CMAKE_BINARY_DIR}/locale/zh_CN/LC_MESSAGES/sfd_tool.mo
        DESTINATION ${CMAKE_INSTALL_LOCALEDIR}/zh_CN/LC_MESSAGES)
endif()

if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man_sfd-tool.1)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man_sfd-tool.1
        DESTINATION ${CMAKE_INSTALL_MANDIR}/man1 RENAME sfd-tool.1)
endif()

find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)
if (GTK_UPDATE_ICON_CACHE)
    add_custom_target(update-icon-cache
        COMMAND ${GTK_UPDATE_ICON_CACHE} -q -t -f ${CMAKE_INSTALL_DATADIR}/icons/hicolor
        COMMENT "Updating GTK icon cache")
endif()