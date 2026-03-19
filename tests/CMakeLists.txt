# 为测试可执行补充 core 之外运行所需的公共实现
set(SFD_TEST_COMMON_SOURCES
    test_stubs.cpp
)

# Windows x86/Channel9 模式下，测试也需要链接 BMPlatform / Wrapper
if (WIN32 AND NOT USE_LIBUSB)
    list(APPEND SFD_TEST_COMMON_SOURCES
        ${CMAKE_SOURCE_DIR}/BMPlatform.cpp
        ${CMAKE_SOURCE_DIR}/Wrapper.cpp
    )
endif()

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

add_executable(test_partition_backup_paths
    test_partition_backup_paths.cpp
    ${SFD_TEST_COMMON_SOURCES}
)

target_link_libraries(test_partition_backup_paths PRIVATE core_lib)

add_executable(test_blocksize_and_backup_service
    test_blocksize_and_backup_service.cpp
    ${SFD_TEST_COMMON_SOURCES}
)
target_link_libraries(test_blocksize_and_backup_service PRIVATE core_lib)

add_test(NAME pac_extract_tests COMMAND test_pac_extract)
add_test(NAME spd_protocol_tests COMMAND test_spd_protocol)
add_test(NAME partition_backup_paths_tests COMMAND test_partition_backup_paths)
add_test(NAME blocksize_and_backup_service_tests COMMAND test_blocksize_and_backup_service)

# ====== 链接运行时依赖（与主程序保持一致的最小子集） ======

set(SFD_TEST_TARGETS
    test_pac_extract
    test_spd_protocol
    test_partition_backup_paths
    test_blocksize_and_backup_service
)
