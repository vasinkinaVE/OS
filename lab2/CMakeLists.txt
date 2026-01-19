cmake_minimum_required(VERSION 3.10)
project(background_runner CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(background_runner STATIC
    background_runner.cpp
)

if(WIN32)
    target_link_libraries(background_runner kernel32)
endif()

add_executable(test_runner
    test_runner.cpp
)

target_link_libraries(test_runner background_runner)

if(MSVC)
    target_compile_options(background_runner PRIVATE /W4)
    target_compile_options(test_runner PRIVATE /W4)
else()
    target_compile_options(background_runner PRIVATE -Wall -Wextra -pedantic)
    target_compile_options(test_runner PRIVATE -Wall -Wextra -pedantic)
endif()