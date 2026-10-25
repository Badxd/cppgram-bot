cmake_minimum_required(VERSION 3.16)
project(cppgram_bot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Either point CPPGRAM_SOURCE_DIR at a cppgram checkout, or install cppgram and let find_package find it.
if(CPPGRAM_SOURCE_DIR)
    add_subdirectory(${CPPGRAM_SOURCE_DIR} cppgram-build)
else()
    find_package(cppgram CONFIG REQUIRED)
endif()

add_executable(cppgram_bot src/main.cpp)
target_link_libraries(cppgram_bot PRIVATE cppgram::cppgram)
