cmake_minimum_required(VERSION 3.16)
project(c_wallet LANGUAGES C CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB KEYPAIR_SOURCES CONFIGURE_DEPENDS "keypair/*.c")
file(GLOB SOLANA_SOURCES CONFIGURE_DEPENDS "solana/*.c")

# Disable vendored tests by default, but allow opting back in.
option(ENABLE_VENDOR_TESTS "Build vendored tilibs test targets" OFF)
if(ENABLE_VENDOR_TESTS)
    set(BUILD_TESTS ON CACHE BOOL "Enable subproject tests" FORCE)
else()
    set(BUILD_TESTS OFF CACHE BOOL "Disable subproject tests" FORCE)
endif()

add_subdirectory(tilibs)

add_library(solana STATIC ${SOLANA_SOURCES})

target_include_directories(solana PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/solana
)

add_executable(main
    main.c
    calc_session.c
    calc_string_store.c
    wallet_crypto.c
    ${KEYPAIR_SOURCES}
)

target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/keypair
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libticables/trunk/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libticalcs/trunk/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libticonv/trunk/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libtifiles/trunk/src
)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(CURL REQUIRED)
pkg_check_modules(glib REQUIRED IMPORTED_TARGET glib-2.0)

target_link_libraries(solana PUBLIC CURL::libcurl)

# Static library for FFI consumption (Rust UI)
add_library(cwallet STATIC
    calc_session.c
    calc_string_store.c
    wallet_crypto.c
    ${KEYPAIR_SOURCES}
)

target_include_directories(cwallet PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/keypair
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libticables/trunk/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libticalcs/trunk/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libticonv/trunk/src
    ${CMAKE_CURRENT_SOURCE_DIR}/tilibs/libtifiles/trunk/src
)

target_link_libraries(cwallet PUBLIC
    ticalcs2
    tifiles2
    ticables2
    ticonv
    Threads::Threads
    PkgConfig::glib
    solana
)

target_link_libraries(main PRIVATE
    ticalcs2
    tifiles2
    ticables2
    ticonv
    Threads::Threads
    PkgConfig::glib
    solana
)
