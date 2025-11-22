add_executable(control_center
    control_center.cpp
    http.cpp
    websocket_client.cpp
    uuid.cpp
)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

target_link_libraries(control_center PRIVATE
    common
    ${CURL_LIBRARIES}
    OpenSSL::SSL
    OpenSSL::Crypto
    pthread
)
