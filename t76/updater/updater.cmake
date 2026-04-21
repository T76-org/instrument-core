set(T76_UPDATER_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE INTERNAL "instrument-core updater module directory")

function(t76_add_stage3_updater_bootloader target)
    set(one_value_args
        APPLICATION_FLASH_OFFSET
        FLASH_SIZE
        PROTECTED_TAIL_SIZE
        USB_VENDOR_ID
        USB_PRODUCT_ID
        USB_MANUFACTURER_STRING
        USB_PRODUCT_STRING
        PROGRAM_NAME
        PROGRAM_VERSION
    )
    cmake_parse_arguments(T76_UPDATER "" "${one_value_args}" "" ${ARGN})

    foreach(required_arg IN LISTS one_value_args)
        if(NOT DEFINED T76_UPDATER_${required_arg})
            message(FATAL_ERROR "t76_add_stage3_updater_bootloader missing ${required_arg}")
        endif()
    endforeach()

    add_executable(${target}
        ${T76_UPDATER_MODULE_DIR}/stage3_bootloader.c
    )

    pico_set_program_name(${target} "${T76_UPDATER_PROGRAM_NAME}")
    pico_set_program_version(${target} "${T76_UPDATER_PROGRAM_VERSION}")
    pico_enable_stdio_usb(${target} 0)
    pico_enable_stdio_uart(${target} 0)

    target_include_directories(${target} PRIVATE
        ${T76_UPDATER_MODULE_DIR}
    )

    target_compile_definitions(${target} PRIVATE
        LIB_TINYUSB_DEVICE=1
        T76_IC_USB_VENDOR_ID=${T76_UPDATER_USB_VENDOR_ID}
        T76_IC_USB_PRODUCT_ID=${T76_UPDATER_USB_PRODUCT_ID}
        T76_IC_USB_MANUFACTURER_STRING="${T76_UPDATER_USB_MANUFACTURER_STRING}"
        T76_IC_USB_PRODUCT_STRING="${T76_UPDATER_USB_PRODUCT_STRING}"
        T76_UPDATER_APPLICATION_FLASH_OFFSET_BYTES=${T76_UPDATER_APPLICATION_FLASH_OFFSET}
        T76_UPDATER_FLASH_SIZE_BYTES=${T76_UPDATER_FLASH_SIZE}
        T76_UPDATER_PROTECTED_TAIL_BYTES=${T76_UPDATER_PROTECTED_TAIL_SIZE}
    )

    target_link_libraries(${target}
        hardware_flash
        hardware_sync
        pico_stdlib
        pico_unique_id
        tinyusb_board
        tinyusb_device
        t76_ic_updater
    )

    pico_add_extra_outputs(${target})
endfunction()

function(t76_add_combined_uf2 target)
    set(one_value_args OUTPUT BOOTLOADER_UF2 APP_UF2)
    cmake_parse_arguments(T76_COMBINED "" "${one_value_args}" "" ${ARGN})

    foreach(required_arg IN LISTS one_value_args)
        if(NOT DEFINED T76_COMBINED_${required_arg})
            message(FATAL_ERROR "t76_add_combined_uf2 missing ${required_arg}")
        endif()
    endforeach()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_custom_command(
        OUTPUT ${T76_COMBINED_OUTPUT}
        COMMAND ${Python3_EXECUTABLE}
            ${T76_UPDATER_MODULE_DIR}/combine_uf2.py
            --output ${T76_COMBINED_OUTPUT}
            ${T76_COMBINED_BOOTLOADER_UF2}
            ${T76_COMBINED_APP_UF2}
        DEPENDS
            ${T76_COMBINED_BOOTLOADER_UF2}
            ${T76_COMBINED_APP_UF2}
            ${T76_UPDATER_MODULE_DIR}/combine_uf2.py
        COMMENT "Combining resident updater bootloader and application UF2 images"
    )

    add_custom_target(${target} ALL
        DEPENDS ${T76_COMBINED_OUTPUT}
    )
endfunction()
