cmake_minimum_required(VERSION 3.22)

include(${CMAKE_SOURCE_DIR}/cmake/mcuboot.cmake)

set(BOOTLOADER_ELF mcuboot)

add_executable(${BOOTLOADER_ELF})

set(BOOT_HAL_SRC
    ${CMAKE_SOURCE_DIR}/Core/Src/system_stm32g4xx.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_cortex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_rcc_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_flash_ramfunc.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_gpio.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_pwr_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_uart_ex.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_exti.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma.c
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Src/stm32g4xx_hal_dma_ex.c
)

target_sources(${BOOTLOADER_ELF} PRIVATE
    main.c
    ${CMAKE_SOURCE_DIR}/startup_stm32g474xx.s
    ${CMAKE_SOURCE_DIR}/Core/Src/syscalls.c
    ${CMAKE_SOURCE_DIR}/Core/Src/sysmem.c
    ${BOOT_HAL_SRC}
    ${MCUBOOT_BOOTUTIL_SOURCES}
)

target_include_directories(${BOOTLOADER_ELF} PRIVATE
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32G4xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32G4xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
    ${MCUBOOT_PORT_INCLUDES}
)

target_compile_definitions(${BOOTLOADER_ELF} PRIVATE
    USE_HAL_DRIVER
    STM32G474xx
    CONFIG_MCUBOOT=1
)

mcuboot_apply_bootloader_includes(${BOOTLOADER_ELF})

target_link_options(${BOOTLOADER_ELF} PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32G474XX_BOOT.ld
    -Wl,-Map=${BOOTLOADER_ELF}.map
    -Wl,--gc-sections
)

target_link_libraries(${BOOTLOADER_ELF} PRIVATE m)

add_custom_command(TARGET ${BOOTLOADER_ELF} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${BOOTLOADER_ELF}> ${CMAKE_CURRENT_BINARY_DIR}/${BOOTLOADER_ELF}.bin
    COMMENT "Generating mcuboot.bin"
)
