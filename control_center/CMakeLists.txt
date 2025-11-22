add_executable(control_center
    control_center.cpp
    uuid.cpp
)

target_link_libraries(control_center PRIVATE
    common
    pthread
)
