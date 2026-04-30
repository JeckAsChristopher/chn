cmake_minimum_required(VERSION 3.15)

project(CHN VERSION 1.0 LANGUAGES C)

# C Standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

# Output folders
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Source path
set(CHN_SRC_DIR ${CMAKE_SOURCE_DIR}/src/v1.0)

# Sources
set(CHN_SOURCES
    ${CHN_SRC_DIR}/main.c
    ${CHN_SRC_DIR}/lexer.c
    ${CHN_SRC_DIR}/parser.c
    ${CHN_SRC_DIR}/ast.c
    ${CHN_SRC_DIR}/compiler.c
    ${CHN_SRC_DIR}/bytecode.c
    ${CHN_SRC_DIR}/vm.c
    ${CHN_SRC_DIR}/gc.c
    ${CHN_SRC_DIR}/func.c
    ${CHN_SRC_DIR}/native.c
    ${CHN_SRC_DIR}/native_net.c
    ${CHN_SRC_DIR}/error.c
)

# Executable
add_executable(chn ${CHN_SOURCES})

# Include headers
target_include_directories(chn PRIVATE ${CHN_SRC_DIR})

# Compiler warnings
if(MSVC)
    target_compile_options(chn PRIVATE /W4)
else()
    target_compile_options(chn PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Threads
find_package(Threads REQUIRED)
target_link_libraries(chn PRIVATE Threads::Threads)

# Math library (Linux/macOS)
if(UNIX AND NOT APPLE)
    target_link_libraries(chn PRIVATE m dl)
elseif(APPLE)
    target_link_libraries(chn PRIVATE m)
endif()

# OpenSSL (required by native_net.c)
find_package(OpenSSL REQUIRED)
target_link_libraries(chn PRIVATE OpenSSL::SSL OpenSSL::Crypto)

# Windows socket support
if(WIN32)
    target_link_libraries(chn PRIVATE ws2_32 crypt32)
endif()

# Install
install(TARGETS chn DESTINATION bin)
