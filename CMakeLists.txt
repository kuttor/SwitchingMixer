cmake_minimum_required(VERSION 3.16)
project(SwitchingMixer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disting NT API location (set this to your API path)
set(DISTING_NT_API_PATH "" CACHE PATH "Path to distingNT_API")

if(NOT DISTING_NT_API_PATH)
    message(WARNING "DISTING_NT_API_PATH not set. Using placeholder include path.")
    set(DISTING_NT_API_PATH "${CMAKE_SOURCE_DIR}")
endif()

# Plugin source
add_library(SwMx SHARED
    SwitchingMixer.cpp
)

target_include_directories(SwMx PRIVATE
    ${DISTING_NT_API_PATH}/include
)

# Output as .ntplugin
set_target_properties(SwMx PROPERTIES
    PREFIX ""
    SUFFIX ".ntplugin"
)

# ARM cross-compilation settings (uncomment for actual build)
# set(CMAKE_SYSTEM_NAME Generic)
# set(CMAKE_SYSTEM_PROCESSOR arm)
# set(CMAKE_C_COMPILER arm-none-eabi-gcc)
# set(CMAKE_CXX_COMPILER arm-none-eabi-g++)
# 
# target_compile_options(SwMx PRIVATE
#     -mcpu=cortex-m7
#     -mthumb
#     -mfpu=fpv5-d16
#     -mfloat-abi=hard
#     -O2
#     -fno-exceptions
#     -fno-rtti
# )

# Installation
install(TARGETS SwMx
    LIBRARY DESTINATION plugins
)
