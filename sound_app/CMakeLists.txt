add_executable(sound_app
    sound_app.cpp
    aplay.cpp
    opus.cpp
    record.cpp
)

target_link_libraries(sound_app PRIVATE
    common
    asound
    opus
)
