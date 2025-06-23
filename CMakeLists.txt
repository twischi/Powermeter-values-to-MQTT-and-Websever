# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.26)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Make CONFIG_.. config-variables from menuconfig available to cmake
include($ENV{IDF_PATH}/tools/cmake/kconfig.cmake)

# Make myComponents-Folder available >> This folder CAN contain Components to be shared X-Projects
set(HOME_DIR $ENV{HOME})                                     # Get the HOME directory of the 'machine' >> used with next line
set(EXTRA_COMPONENT_DIRS "${HOME_DIR}/ESP-IDF/myComponents") # Set 'myComponents'-Path as 'EXTRA_COMPONENT_DIRS'    Use components relative to HOME

#------------------------------------------------------
# SET THE PROJECT NAME from the Folder-Name of Project
#------------------------------------------------------
get_filename_component(PROJECT_NAME_FROM_DIR ${CMAKE_SOURCE_DIR} NAME) # Get the last part of the Source-Directory
project(${PROJECT_NAME_FROM_DIR} )                                     # Set the Project-Name

# message(FATAL_ERROR "*** PRJ-Name *** Stop here for testing purposes")      