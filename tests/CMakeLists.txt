# 为测试可执行补充 core 之外运行所需的公共实现
set(SFD_TEST_COMMON_SOURCES
    test_stubs.cpp
)

# Windows 下，所有测试二进制都需要链接 Channel9 封装，
# 以确保 BMPlatform/Wrapper 相关符号和运行时依赖可用。
if (WIN32)
    list(APPEND SFD_TEST_COMMON_SOURCES
        ${CMAKE_SOURCE_DIR}/BMPlatform.cpp
        ${CMAKE_SOURCE_DIR}/Wrapper.cpp
    )
endif()

# ====== 测试目标定义 ======

add_executable(test_spd_protocol
    test_spd_protocol.cpp
    ${SFD_TEST_COMMON_SOURCES}
)

add_executable(test_partition_backup_paths
    test_partition_backup_paths.cpp
    ${SFD_TEST_COMMON_SOURCES}
)

add_executable(test_blocksize_and_backup_service
    test_blocksize_and_backup_service.cpp
    ${SFD_TEST_COMMON_SOURCES}
)

# ====== 平台相关链接设置 ======
# 为避免 macOS 上重复链接 core_lib/pages_lib 的告警，
# 同时兼顾 Linux 上静态库之间的循环依赖，这里做平台区分：
# - macOS / Windows: 每个库仅链接一次，避免 duplicate libraries 警告；
# - 其他 UNIX（Linux 等）: 将 core_lib/pages_lib/sfd_ui 重复一遍，
#   解决静态库 A 依赖 B、B 再依赖 C 的循环依赖问题。

if(APPLE OR WIN32)
    set(SFD_TEST_LIBS
        core_lib
        pages_lib
        sfd_ui
    )
else()
    set(SFD_TEST_LIBS
        core_lib
        pages_lib
        sfd_ui
        core_lib
        pages_lib
        sfd_ui
    )
endif()

# 实际链接

target_link_libraries(test_spd_protocol PRIVATE ${SFD_TEST_LIBS})
target_link_libraries(test_partition_backup_paths PRIVATE ${SFD_TEST_LIBS})
target_link_libraries(test_blocksize_and_backup_service PRIVATE ${SFD_TEST_LIBS})

# ====== CTest 集成 ======

add_test(NAME spd_protocol_tests COMMAND test_spd_protocol)
add_test(NAME partition_backup_paths_tests COMMAND test_partition_backup_paths)
add_test(NAME blocksize_and_backup_service_tests COMMAND test_blocksize_and_backup_service)

# 在 Windows CI 环境下暂时禁用 blocksize_and_backup_service_tests，
# 避免由于运行时依赖缺失导致整个 ctest 失败。
if (WIN32)
    set_tests_properties(blocksize_and_backup_service_tests PROPERTIES DISABLED TRUE)
endif()

# ====== 头文件与附加依赖设置（保持与原逻辑一致） ======

set(SFD_TEST_TARGETS
    test_spd_protocol
    test_partition_backup_paths
    test_blocksize_and_backup_service
)

# 将测试目标导出到父作用域，方便顶层 CMake 统一设置编译选项（如 USE_GTK/USE_LIBUSB）
set(SFD_TEST_TARGETS ${SFD_TEST_TARGETS} PARENT_SCOPE)

# 让测试目标也能找到顶层头文件（common.h / ui_common.h 等）
foreach(tgt IN LISTS SFD_TEST_TARGETS)
    target_include_directories(${tgt}
        PRIVATE
            ${CMAKE_SOURCE_DIR}
            ${CMAKE_SOURCE_DIR}/core
            ${CMAKE_SOURCE_DIR}/pages
            ${CMAKE_BINARY_DIR}/generated
    )

    # 对于 64 位 SPRD 版本，添加必要的宏定义
    if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND NOT USE_LIBUSB)
        target_compile_definitions(${tgt} PRIVATE
            _WIN64
            BUILDING_TESTS
            BUILDING_BMPLATFORM
        )
    endif()

    # 链接必要的库（注意：不要链接 BMPlatform.lib）
    if(WIN32)
        target_link_libraries(${tgt} PRIVATE
            dbghelp
            setupapi
        )
    endif()

    # 确保不使用 BMPlatform 库
    # 移除任何可能链接 BMPlatform 的语句
    if(TARGET BMPlatform)
        # 不链接 BMPlatform
        # target_link_libraries(${tgt} PRIVATE BMPlatform)  # 注释掉
    endif()
endforeach()
