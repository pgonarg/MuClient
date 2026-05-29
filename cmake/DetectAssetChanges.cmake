# Detect changes in converted glTF assets
# Usage: include(DetectAssetChanges)
#        detect_gltf_changes(SOURCE_DIR OUTPUT_DIR CHANGED_FILES_VAR)
#
# This script scans the glTF source directory for files that are newer than
# the build timestamp, indicating they've been modified.

function(detect_gltf_changes SOURCE_DIR OUTPUT_DIR CHANGED_FILES_VAR)
    if (NOT EXISTS "${SOURCE_DIR}")
        message(STATUS "glTF source directory not found: ${SOURCE_DIR}")
        set(${CHANGED_FILES_VAR} "" PARENT_SCOPE)
        return()
    endif()

    # Find all glTF files
    file(GLOB_RECURSE GLTF_FILES
        "${SOURCE_DIR}/*.glb"
        "${SOURCE_DIR}/*.gltf"
        "${SOURCE_DIR}/*.bin")

    if (NOT GLTF_FILES)
        message(STATUS "No glTF files found in ${SOURCE_DIR}")
        set(${CHANGED_FILES_VAR} "" PARENT_SCOPE)
        return()
    endif()

    set(TIMESTAMP_FILE "${OUTPUT_DIR}/.gltf_build_timestamp")
    set(CHANGED_FILES "")

    # Check each file's timestamp
    foreach(GLTF_FILE ${GLTF_FILES})
        # On first build, include all files
        if(NOT EXISTS "${TIMESTAMP_FILE}")
            list(APPEND CHANGED_FILES "${GLTF_FILE}")
            message(STATUS "[glTF Assets] First build - including: ${GLTF_FILE}")
        else()
            # Compare file modification time with build timestamp
            file(TIMESTAMP "${TIMESTAMP_FILE}" BUILD_TIME "%s")
            file(TIMESTAMP "${GLTF_FILE}" FILE_TIME "%s")

            # If file is newer than build timestamp, it was modified
            if(FILE_TIME GREATER BUILD_TIME)
                list(APPEND CHANGED_FILES "${GLTF_FILE}")
                message(STATUS "[glTF Assets] Modified: ${GLTF_FILE}")
            endif()
        endif()
    endforeach()

    # Report results
    list(LENGTH CHANGED_FILES NUM_CHANGED)
    if(NUM_CHANGED GREATER 0)
        message(STATUS "[glTF Assets] Found ${NUM_CHANGED} changed/new assets")
    else()
        message(STATUS "[glTF Assets] No changes detected")
    endif()

    set(${CHANGED_FILES_VAR} "${CHANGED_FILES}" PARENT_SCOPE)
endfunction()
