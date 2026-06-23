set(MCUBOOT_ROOT ${CMAKE_SOURCE_DIR}/ThirdParty/mcuboot)
set(MCUBOOT_BOOTUTIL ${MCUBOOT_ROOT}/boot/bootutil)
set(MCUBOOT_TINYCRYPT ${MCUBOOT_ROOT}/ext/tinycrypt/lib)

set(MCUBOOT_PORT_DIR ${CMAKE_SOURCE_DIR}/Components/mcuboot_port)

set(MCUBOOT_PORT_INCLUDES
    ${MCUBOOT_PORT_DIR}/include
    ${MCUBOOT_PORT_DIR}/Inc
    ${MCUBOOT_BOOTUTIL}/include
    ${MCUBOOT_BOOTUTIL}/src
    ${MCUBOOT_TINYCRYPT}/include
)

set(MCUBOOT_BOOTUTIL_SOURCES
    ${MCUBOOT_BOOTUTIL}/src/bootutil_misc.c
    ${MCUBOOT_BOOTUTIL}/src/bootutil_public.c
    ${MCUBOOT_BOOTUTIL}/src/bootutil_area.c
    ${MCUBOOT_BOOTUTIL}/src/bootutil_loader.c
    ${MCUBOOT_BOOTUTIL}/src/bootutil_img_hash.c
    ${MCUBOOT_BOOTUTIL}/src/image_validate.c
    ${MCUBOOT_BOOTUTIL}/src/tlv.c
    ${MCUBOOT_BOOTUTIL}/src/caps.c
    ${MCUBOOT_BOOTUTIL}/src/fault_injection_hardening.c
    ${MCUBOOT_BOOTUTIL}/src/loader.c
    ${MCUBOOT_PORT_DIR}/Src/flash_map_backend.c
    ${MCUBOOT_PORT_DIR}/Src/flash_hal.c
    ${MCUBOOT_PORT_DIR}/Src/bootutil_port.c
    ${MCUBOOT_TINYCRYPT}/source/sha256.c
    ${MCUBOOT_TINYCRYPT}/source/utils.c
)

set(MCUBOOT_APPUTIL_SOURCES
)

if(NOT FW_VERSION)
    file(READ "${CMAKE_SOURCE_DIR}/VERSION" FW_VERSION)
    string(STRIP "${FW_VERSION}" FW_VERSION)
endif()

function(mcuboot_apply_bootloader_includes target)
    target_include_directories(${target} PRIVATE ${MCUBOOT_PORT_INCLUDES})
    target_compile_definitions(${target} PRIVATE CONFIG_MCUBOOT=1)
endfunction()

function(mcuboot_apply_app_includes target)
    target_include_directories(${target} PRIVATE ${MCUBOOT_PORT_INCLUDES})
endfunction()

function(mcuboot_add_app_signing target)
    if(NOT PYTHON3_EXECUTABLE)
        return()
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${target}> ${target}.bin
        COMMAND ${PYTHON3_EXECUTABLE} ${MCUBOOT_ROOT}/scripts/imgtool.py create
            --align 8
            --version ${FW_VERSION}
            --header-size 0x200
            --slot-size 0x34000
            --pad-header
            ${target}.bin
            ${target}.signed.bin
        COMMAND ${PYTHON3_EXECUTABLE} ${MCUBOOT_ROOT}/scripts/imgtool.py create
            --align 8
            --version ${FW_VERSION}
            --header-size 0x200
            --slot-size 0x40000
            --pad-header
            ${target}.bin
            ${target}-ota.signed.bin
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Signing application images (slot0 + OTA slot1) v${FW_VERSION}"
    )
endfunction()
