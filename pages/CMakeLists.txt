add_library(pages_lib STATIC
    page_connect.cpp
    page_partition.cpp
    page_manual.cpp
    page_advanced_op.cpp
    page_advanced_set.cpp
    page_debug.cpp
    page_about.cpp
    page_log.cpp
    page_pac_flash.cpp
)

target_include_directories(pages_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core
)
