idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ".")

# Make the Project-FULL-Path availabe for compiler MACROs  > PRJ_PATH
add_compile_definitions(PRJ_PATH="${CMAKE_SOURCE_DIR}")

# Get only the Project Directory (=last folder name)
get_filename_component(PROJECT_DIR_NAME ${CMAKE_SOURCE_DIR} NAME)

# Make it availabe for compiler MACROs                     > PROJECT_DIR_NAME
add_compile_definitions(PROJECT_DIR_NAME="${PROJECT_DIR_NAME}")

# Add files to be used at runtime. Provided in folder 'Storage_RT'
# Notes: (1) Use Partiion-Name 'storage'
#        (2) Add Partion to partition table 
#        (3) Bin will be created: /build/storage.bin
littlefs_create_partition_image(storage ../storage_at_runtime FLASH_IN_PROJECT)