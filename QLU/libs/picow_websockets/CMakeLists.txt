project(picow_websockets C)
set(PICO_BOARD pico_w CACHE STRING "Board type")

set(FREERTOS_KERNEL_PATH ${CMAKE_SOURCE_DIR}/libs/FreeRTOS-Kernel)
include(${FREERTOS_KERNEL_PATH}/portable/ThirdParty/GCC/RP2040/FreeRTOS_Kernel_import.cmake)

add_library(picow_websockets STATIC
    b64.h
    encrypt.h
    tenysha1.h
    websocket.h
    packet_ops.c
    websocket.c
)

target_include_directories(picow_websockets
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_SOURCE_DIR}/configs
)

target_link_libraries(picow_websockets
    PUBLIC
        pico_stdlib
        FreeRTOS-Kernel
        pico_cyw43_driver
        pico_cyw43_arch_lwip_poll
)