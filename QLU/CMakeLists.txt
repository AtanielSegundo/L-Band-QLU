# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(QLU C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1

add_executable(QLU 
               src/QLU.c )

pico_set_program_name(QLU "QLU")
pico_set_program_version(QLU "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(QLU 0)
pico_enable_stdio_usb(QLU 1)

# Add the standard library to the build
target_link_libraries(QLU
        ${CMAKE_CURRENT_LIST_DIR}
        pico_stdlib)

# Add the standard include files to the build
target_include_directories(QLU PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/includes
        ${CMAKE_CURRENT_LIST_DIR}/routes
        ${CMAKE_CURRENT_LIST_DIR}/configs
)

add_subdirectory(libs/ST7735)
add_subdirectory(libs/QRcode)
# add_subdirectory(libs/picow_websockets)

# Add any user requested libraries

add_compile_options(-Wall
        -Wno-format          
        -Wno-unused-function 
        -Wno-maybe-uninitialized
        )

target_link_libraries(QLU 
        hardware_spi
        hardware_dma
        # picow_websockets
        st7735
        gfx
        QRcode
        )

pico_add_extra_outputs(QLU)

