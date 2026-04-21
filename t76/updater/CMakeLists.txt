set(LIBRARY_NAME t76_ic_updater)

add_library(${LIBRARY_NAME} INTERFACE)

target_include_directories(${LIBRARY_NAME} INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_CURRENT_LIST_DIR}/updater.cmake)
