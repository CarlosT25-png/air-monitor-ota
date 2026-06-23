cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

set(CMAKE_PROJECT_NAME air-monitor-ota)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/Components/esp32-at/Inc/configuration.h")
    message(FATAL_ERROR
        "Missing Components/esp32-at/Inc/configuration.h — "
        "copy configuration.h.example to configuration.h and edit your settings.")
endif()

project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

get_filename_component(TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
set(CMAKE_OBJCOPY "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-objcopy")
set(CMAKE_SIZE "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-size")

enable_language(C ASM)

include(${CMAKE_SOURCE_DIR}/cmake/mcuboot.cmake)
message("Firmware version: ${FW_VERSION}")

add_executable(${CMAKE_PROJECT_NAME})
add_subdirectory(cmake/stm32cubemx)
add_subdirectory(bootloader)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Components/esp32-at/Src/esp32-at.c
    Components/dht11/Src/dht11.c
    Components/flash/Src/flash.c
    Components/ota/Src/ota.c
    Components/ota/Src/ota_cbor.c
    Components/mcuboot_app/Src/mcuboot_app.c
    ${MCUBOOT_BOOTUTIL}/src/bootutil_public.c
    ${MCUBOOT_PORT_DIR}/Src/flash_map_backend.c
    ${MCUBOOT_PORT_DIR}/Src/flash_hal.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    Components/esp32-at/Inc
    Components/dht11/Inc
    Components/flash/Inc
    Components/ota/Inc
    Components/mcuboot_app/Inc
    ${MCUBOOT_PORT_INCLUDES}
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USER_VECT_TAB_ADDRESS
    VECT_TAB_BASE_ADDRESS=0x08000000U
    VECT_TAB_OFFSET=0x0000C200U
    MCUBOOT_HAVE_LOGGING=0
    FW_VERSION_STR="${FW_VERSION}"
)

mcuboot_apply_app_includes(${CMAKE_PROJECT_NAME})

list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    "-T${CMAKE_SOURCE_DIR}/STM32G474XX_APP.ld"
    "-Wl,-Map=${CMAKE_PROJECT_NAME}.map"
)

find_program(PYTHON3_EXECUTABLE python3)
if(PYTHON3_EXECUTABLE)
    mcuboot_add_app_signing(${CMAKE_PROJECT_NAME})
endif()
