cmake_minimum_required(VERSION 3.16)
project(cppgram_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prebuilt cppgram: point CPPGRAM_LIB_DIR at the folder with libcppgram.a and CPPGRAM_INCLUDE_DIR at its include/ folder.
set(CPPGRAM_LIB_DIR "" CACHE PATH "folder containing libcppgram.a")
set(CPPGRAM_INCLUDE_DIR "" CACHE PATH "cppgram include/ folder")
if(NOT CPPGRAM_LIB_DIR OR NOT CPPGRAM_INCLUDE_DIR)
    message(FATAL_ERROR "set CPPGRAM_LIB_DIR and CPPGRAM_INCLUDE_DIR")
endif()

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

add_library(cppgram_prebuilt STATIC IMPORTED)
set_target_properties(cppgram_prebuilt PROPERTIES
    IMPORTED_LOCATION "${CPPGRAM_LIB_DIR}/libcppgram.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CPPGRAM_INCLUDE_DIR}")
target_link_libraries(cppgram_prebuilt INTERFACE
    CURL::libcurl OpenSSL::SSL OpenSSL::Crypto ZLIB::ZLIB SQLite::SQLite3 Threads::Threads)

add_executable(cppgram_bot src/main.cpp)
target_link_libraries(cppgram_bot PRIVATE cppgram_prebuilt)
