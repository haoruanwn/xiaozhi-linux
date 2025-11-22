find_package(PkgConfig REQUIRED)
pkg_check_modules(ALSA REQUIRED alsa)
pkg_check_modules(OPUS REQUIRED opus)

add_executable(sound_app
    sound_app.cpp
    aplay.cpp
    opus.cpp
    record.cpp
)


target_link_libraries(sound_app PRIVATE
    common  # 你的内部库
    ${ALSA_LIBRARIES}
    ${OPUS_LIBRARIES}
)

target_include_directories(sound_app PRIVATE
    ${ALSA_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
)
