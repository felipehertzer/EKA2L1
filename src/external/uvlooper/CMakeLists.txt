add_library(uvlooper STATIC
    src/uvlooper.cpp)
target_include_directories(uvlooper PUBLIC include)
if (libuv_SOURCE_DIR)
    target_include_directories(uvlooper PUBLIC "${libuv_SOURCE_DIR}/include")
endif()
target_link_libraries(uvlooper PUBLIC uv_a)
