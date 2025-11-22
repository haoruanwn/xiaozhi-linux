cmake_minimum_required(VERSION 3.10)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lv_100ask_xz_ai)
add_subdirectory(lvgl)

target_include_directories(lvgl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(gui_app main.c mouse_cursor_icon.c)

target_link_libraries(gui_app PRIVATE
    lv_100ask_xz_ai
    lvgl
    lvgl::examples
    lvgl::demos
    # lvgl::thorvg # Uncomment if needed
    common
    m
    pthread
)
