aux_source_directory(. SRC_LIST)

add_executable(main ${SRC_LIST})

TARGET_LINK_LIBRARIES(main pthread)