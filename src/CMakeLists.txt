# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
                       REQUIRES fatfs
                       esp_driver_sdmmc
                       esp_driver_spi
                       esp_driver_sdspi
                       sdmmc
)
