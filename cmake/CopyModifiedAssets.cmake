# Copy modified glTF assets to build output directory
# Usage: cmake -DSOURCE_DIR=... -DOUTPUT_DIR=... -P CopyModifiedAssets.cmake
#
# This script is called during the build to copy any modified glTF files
# from the source directory to the build output directory.

include(${CMAKE_CURRENT_LIST_DIR}/DetectAssetChanges.cmake)

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR not specified")
endif()

if(NOT OUTPUT_DIR)
    message(FATAL_ERROR "OUTPUT_DIR not specified")
endif()

message(STATUS "[glTF Copy] Source: ${SOURCE_DIR}")
message(STATUS "[glTF Copy] Destination: ${OUTPUT_DIR}")

# Detect changes
detect_gltf_changes("${SOURCE_DIR}" "${OUTPUT_DIR}" CHANGED_FILES)

if(CHANGED_FILES)
    message(STATUS "[glTF Copy] Processing ${CMAKE_VERSION} modified assets...")

    # Copy each changed file, preserving directory structure
    foreach(SOURCE_FILE ${CHANGED_FILES})
        # Calculate relative path from source directory
        file(RELATIVE_PATH RELATIVE_PATH "${SOURCE_DIR}" "${SOURCE_FILE}")

        # Destination path preserves structure
        set(DEST_FILE "${OUTPUT_DIR}/Models_gltf/${RELATIVE_PATH}")
        get_filename_component(DEST_DIR "${DEST_FILE}" DIRECTORY)

        # Create destination directory
        file(MAKE_DIRECTORY "${DEST_DIR}")

        # Copy file
        configure_file("${SOURCE_FILE}" "${DEST_FILE}" COPYONLY)
        message(STATUS "[glTF Copy] ✓ ${RELATIVE_PATH}")
    endforeach()

    # Update build timestamp for next build
    file(WRITE "${OUTPUT_DIR}/.gltf_build_timestamp" "${CMAKE_CURRENT_LIST_DIR}")
    message(STATUS "[glTF Copy] Updated timestamp for next build check")
else()
    message(STATUS "[glTF Copy] No modified assets to copy")
endif()

message(STATUS "[glTF Copy] Complete")
