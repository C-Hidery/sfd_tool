include_directories(
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 为测试可执行补充 core 之外运行所需的公共实现
set(SFD_TEST_COMMON_SOURCES
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/common.cpp
    ${CMAKE_SOURCE_DIR}/GtkWidgetHelper.cpp
    ${CMAKE_SOURCE_DIR}/ui_common.cpp
)

add_executable(test_pac_extract
    test_pac_extract.cpp
    ${SFD_TEST_COMMON_SOURCES}
)
target_link_libraries(test_pac_extract PRIVATE core_lib)

add_executable(test_spd_protocol
    test_spd_protocol.cpp
    ${SFD_TEST_COMMON_SOURCES}
)
target_link_libraries(test_spd_protocol PRIVATE core_lib)

add_test(NAME pac_extract_tests COMMAND test_pac_extract)
add_test(NAME spd_protocol_tests COMMAND test_spd_protocol)

# ====== 链接运行时依赖（与主程序保持一致的最小子集） ======

set(SFD_TEST_TARGETS test_pac_extract test_spd_protocol)

# 线程库
find_package(Threads REQUIRED)

if (APPLE)
    foreach(tgt IN LISTS SFD_TEST_TARGETS)
        target_compile_definitions(${tgt} PRIVATE MACOS)
        target_link_libraries(${tgt} PRIVATE
            Threads::Threads
            "-framework CoreFoundation"
            "-framework IOKit"
        )
    endforeach()
elseif (WIN32)
    foreach(tgt IN LISTS SFD_TEST_TARGETS)
        target_link_libraries(${tgt} PRIVATE Threads::Threads)
        # 与主程序保持一致的调试/设备库
        target_link_libraries(${tgt} PRIVATE dbghelp setupapi)
    endforeach()
else()
    foreach(tgt IN LISTS SFD_TEST_TARGETS)
        target_link_libraries(${tgt} PRIVATE Threads::Threads m)
    endforeach()
endif()

# GTK3（仅在启用时链接）
if (USE_GTK)
    if (WIN32)
        if (CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
            set(_gtk_root "C:/msys64/mingw64")
        else()
            set(_gtk_root "C:/msys64/mingw32")
        endif()

        foreach(tgt IN LISTS SFD_TEST_TARGETS)
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
            target_link_libraries(${tgt} PRIVATE
                "${_gtk_root}/lib/libgtk-3.dll.a"
                "${_gtk_root}/lib/libgdk-3.dll.a"
                "${_gtk_root}/lib/libglib-2.0.dll.a"
                "${_gtk_root}/lib/libgobject-2.0.dll.a"
                "${_gtk_root}/lib/libgio-2.0.dll.a"
                "${_gtk_root}/lib/libharfbuzz.dll.a"
                "${_gtk_root}/lib/libpango-1.0.dll.a"
                "${_gtk_root}/lib/libpangocairo-1.0.dll.a"
            )
        endforeach()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)

        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_include_directories(${tgt} PRIVATE ${GTK3_INCLUDE_DIRS})
            target_link_directories(${tgt} PRIVATE ${GTK3_LIBRARY_DIRS})
            target_link_libraries(${tgt} PRIVATE ${GTK3_LIBRARIES})
        endforeach()
    endif()
endif()

# libusb（仅在启用时链接）
if (USE_LIBUSB)
    if (WIN32)
        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=1)
            target_include_directories(${tgt} PRIVATE
                ${CMAKE_SOURCE_DIR}/third_party/Lib
            )
            target_link_libraries(${tgt} PRIVATE
                "${CMAKE_SOURCE_DIR}/third_party/Lib/libusb-1.0.lib"
            )
        endforeach()
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBUSB REQUIRED libusb-1.0)

        foreach(tgt IN LISTS SFD_TEST_TARGETS)
            target_compile_definitions(${tgt} PRIVATE USE_LIBUSB=1)
            target_include_directories(${tgt} PRIVATE ${LIBUSB_INCLUDE_DIRS})
            target_link_directories(${tgt} PRIVATE ${LIBUSB_LIBRARY_DIRS})
            target_link_libraries(${tgt} PRIVATE ${LIBUSB_LIBRARIES})
        endforeach()
    endif()
endif()
