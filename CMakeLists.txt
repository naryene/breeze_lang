cmake_minimum_required(VERSION 3.10)
project(breeze C)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set build type if not set
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Add source files
set(SOURCES
    src/chunk.c
    src/compiler.c
    src/debug.c
    src/main.c
    src/memory.c
    src/object.c
    src/scanner.c
    src/table.c
    src/value.c
    src/virtual_machine.c
)

# Create executable
add_executable(breeze ${SOURCES})

# Add include directories
target_include_directories(breeze PRIVATE src)

# Linux-specific compiler flags
target_compile_options(breeze PRIVATE 
    -Wall 
    -Wextra 
    # -Werror 
    -pedantic
    -g
)

# Optional: Add sanitizers for debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(breeze PRIVATE 
        -fsanitize=address
        -fsanitize=undefined
    )
    target_link_options(breeze PRIVATE 
        -fsanitize=address
        -fsanitize=undefined
    )
endif()

# Install target (optional)
install(TARGETS breeze DESTINATION bin)
