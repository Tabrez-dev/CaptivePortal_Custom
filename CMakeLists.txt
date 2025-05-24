# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(captive_portal)

# Add components directory to the build
set(EXTRA_COMPONENT_DIRS ${PROJECT_SOURCE_DIR}/components)
