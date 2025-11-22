cmake_minimum_required(VERSION 3.14)

project(sound_app)

include(ExternalProject)

# Common install dir for all external deps
set(LOCAL_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/local_install)
set(LOCAL_INCLUDE_DIR ${LOCAL_INSTALL_DIR}/include)
set(LOCAL_LIB_DIR ${LOCAL_INSTALL_DIR}/lib)

file(MAKE_DIRECTORY ${LOCAL_INCLUDE_DIR})
file(MAKE_DIRECTORY ${LOCAL_LIB_DIR})

# Get compiler dir for PATH
get_filename_component(COMPILER_DIR ${CMAKE_C_COMPILER} DIRECTORY)
set(CROSS_ENV_PATH ${COMPILER_DIR}:$ENV{PATH})
set(CROSS_HOST "arm-rockchip830-linux-uclibcgnueabihf")

# --- ALSA ---
set(ALSA_LIBRARY ${LOCAL_LIB_DIR}/libasound.so)
ExternalProject_Add(alsa_project
    URL https://www.alsa-project.org/files/pub/lib/alsa-lib-1.2.9.tar.bz2
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        <SOURCE_DIR>/configure
        --host=${CROSS_HOST}
        --prefix=${LOCAL_INSTALL_DIR}
        --disable-python
        --disable-topology
        CC=${CMAKE_C_COMPILER}
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        make
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        make install
    BUILD_BYPRODUCTS ${ALSA_LIBRARY}
)

# --- Opus ---
set(OPUS_LIBRARY ${LOCAL_LIB_DIR}/libopus.so)
ExternalProject_Add(opus_project
    URL https://downloads.xiph.org/releases/opus/opus-1.4.tar.gz
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        <SOURCE_DIR>/configure
        --host=${CROSS_HOST}
        --prefix=${LOCAL_INSTALL_DIR}
        --disable-extra-programs
        --disable-doc
        CC=${CMAKE_C_COMPILER}
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        make
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        make install
    BUILD_BYPRODUCTS ${OPUS_LIBRARY}
)

# --- SpeexDSP ---
set(SPEEXDSP_LIBRARY ${LOCAL_LIB_DIR}/libspeexdsp.so)
ExternalProject_Add(speexdsp_project
    URL https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz
    CONFIGURE_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        <SOURCE_DIR>/configure
        --host=${CROSS_HOST}
        --prefix=${LOCAL_INSTALL_DIR}
        --disable-examples
        CC=${CMAKE_C_COMPILER}
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        make
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E env PATH=${CROSS_ENV_PATH}
        make install
    BUILD_BYPRODUCTS ${SPEEXDSP_LIBRARY}
)

# --- Main Target ---

add_executable(sound_app
    sound_app.cpp
    aplay.cpp
    opus.cpp
    record.cpp
)

# Add dependencies
add_dependencies(sound_app alsa_project opus_project speexdsp_project)

# Include directories
target_include_directories(sound_app PRIVATE
    ${LOCAL_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/common/include
)

# Link libraries
target_link_libraries(sound_app PRIVATE
    common
    ${ALSA_LIBRARY}
    ${OPUS_LIBRARY}
    ${SPEEXDSP_LIBRARY}
)
