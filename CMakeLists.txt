cmake_minimum_required(VERSION 3.10)
project(wifi LANGUAGES C)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set(WIFI_SOURCES
    src/wifi.c
    src/link_protocol/smart_config.c
    src/link_protocol/http_tp.c
)

add_library(wifi SHARED ${WIFI_SOURCES})

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(wifi PRIVATE
        -Wall
        -Wextra
    )
endif()

target_link_libraries(wifi PRIVATE Threads::Threads OpenSSL::SSL OpenSSL::Crypto)

install(TARGETS wifi
    EXPORT ${SROBOTIS_EXPORT_SET}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(FILES include/wifi.h
    DESTINATION include
)

option(BUILD_TESTS "Build test programs" OFF)

if(BUILD_TESTS)
    add_executable(test_wifi_demo test/test_wifi_demo.c)
    target_link_libraries(test_wifi_demo wifi)
    set_target_properties(test_wifi_demo PROPERTIES BUILD_RPATH "$ORIGIN/../lib")
    install(TARGETS test_wifi_demo RUNTIME DESTINATION bin)
endif()
