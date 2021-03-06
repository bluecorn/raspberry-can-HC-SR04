cmake_minimum_required(VERSION 3.0.0)

## SETTINGS

# Set the project name:

set(PROJECT_NAME uavccan-templae-c)

# Set the name of the executable

set(EXECUTABLE_NAME ultrasound-can-node)

# Set the inlude include directories

set(INLUDE_DIRS libcanard socketcan)

# Set the sources {include the headers}

set(MAIN_SOURCE ./src/main.c)
set(LIBCANARD_SRC libcanard/canard.h libcanard/canard.c)
set(LIB_DSDL_SRC libcanard/canard_dsdl.h libcanard/canard_dsdl.c)
set(SOCKETCAN_SRC socketcan/socketcan.h socketcan/socketcan.c)

find_package(pigpio REQUIRED)

## MAIN BLOCK

project(${PROJECT_NAME} VERSION 0.1.0)
include(CTest)
enable_testing()

include_directories(${INLUDE_DIRS} ${pigpio_INCLUDE_DIRS})
add_executable(${EXECUTABLE_NAME} ${MAIN_SOURCE} ${LIBCANARD_SRC} ${LIB_DSDL_SRC} ${SOCKETCAN_SRC})
target_link_libraries(${EXECUTABLE_NAME} LINK_PRIVATE ${pigpio_LIBRARY})

# Other settings

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
set(CMAKE_C_STANDARD 11)
include(CPack)
