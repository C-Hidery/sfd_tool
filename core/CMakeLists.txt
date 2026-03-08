add_library(core_lib STATIC
    logging.cpp
    file_io.cpp
    pac_extract.cpp
    usb_transport.cpp
    spd_protocol.cpp
)

target_include_directories(core_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
