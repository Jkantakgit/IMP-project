# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)


include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(ESP_EYE)

# Build a SPIFFS partition image from the `spiffs/` folder and include it in the
# flash image so `idf.py flash` programs the frontend into flash automatically.
# The `spiffs` folder should be at the project root and contain `index.html` etc.
if(EXISTS "${CMAKE_SOURCE_DIR}/spiffs")
	# Partition table defines the SPIFFS partition with name 'storage', so
	# generate the image for that partition name.
	spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)
else()
	message(STATUS "No spiffs/ folder found; SPIFFS image not created")
endif()

# Use custom partition table if present
if(EXISTS "${CMAKE_SOURCE_DIR}/partitions.csv")
	set(PARTITION_TABLE_CUSTOM_CSV "${CMAKE_SOURCE_DIR}/partitions.csv" CACHE STRING "Custom partition table CSV")
	message(STATUS "Using custom partition table: ${CMAKE_SOURCE_DIR}/partitions.csv")
endif()
