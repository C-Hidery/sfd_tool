include_directories(
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(test_pac_extract
    test_pac_extract.cpp
)
target_link_libraries(test_pac_extract PRIVATE core_lib)
add_test(NAME pac_extract_tests COMMAND test_pac_extract)

add_executable(test_spd_protocol
    test_spd_protocol.cpp
)
target_link_libraries(test_spd_protocol PRIVATE core_lib)
add_test(NAME spd_protocol_tests COMMAND test_spd_protocol)
