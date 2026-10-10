cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS MODE PLUGIN_FILE INI_FILE README_FILE MINHOOK_LICENSE_FILE OUTPUT_DIR ARCHIVE_FILE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing packaging argument: ${required}")
    endif()
endforeach()

foreach(input IN ITEMS PLUGIN_FILE INI_FILE README_FILE MINHOOK_LICENSE_FILE)
    if(NOT EXISTS "${${input}}")
        message(FATAL_ERROR "Packaging input does not exist: ${${input}}")
    endif()
endforeach()

# Keep the project-wide fixes link in every future release package. The exact
# plain-text sentence is intentional: README.txt must not turn it into Markdown.
file(READ "${README_FILE}" readme_contents)
set(required_misc_line
    "For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)")
string(FIND "${readme_contents}" "Misc" misc_heading_offset)
string(FIND "${readme_contents}" "${required_misc_line}" misc_line_offset)
if(misc_heading_offset EQUAL -1 OR misc_line_offset EQUAL -1)
    message(FATAL_ERROR
        "Release README must retain the plain-text Misc section and fixes URL")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
foreach(stale IN ITEMS
        AmnesiaTDD.FPSFix.asi
        AmnesiaTDD.FPSFix.ini
        README.md
        README.txt
        MINHOOK-LICENSE.txt
        wininet.dll
        THIRD-PARTY-NOTICES.txt
        SHA256SUMS.txt)
    file(REMOVE "${OUTPUT_DIR}/${stale}")
endforeach()
file(COPY_FILE "${PLUGIN_FILE}" "${OUTPUT_DIR}/AmnesiaTDD.FPSFix.asi" ONLY_IF_DIFFERENT)
file(COPY_FILE "${INI_FILE}" "${OUTPUT_DIR}/AmnesiaTDD.FPSFix.ini" ONLY_IF_DIFFERENT)
file(COPY_FILE "${README_FILE}" "${OUTPUT_DIR}/README.txt" ONLY_IF_DIFFERENT)
file(COPY_FILE "${MINHOOK_LICENSE_FILE}" "${OUTPUT_DIR}/MINHOOK-LICENSE.txt"
     ONLY_IF_DIFFERENT)

set(package_files
    "AmnesiaTDD.FPSFix.asi"
    "AmnesiaTDD.FPSFix.ini"
    "README.txt"
    "MINHOOK-LICENSE.txt"
)

if(MODE STREQUAL "ultimate-asi-loader")
    foreach(required IN ITEMS LOADER_FILE NOTICE_FILE)
        if(NOT DEFINED ${required} OR NOT EXISTS "${${required}}")
            message(FATAL_ERROR "Missing Ultimate ASI Loader input: ${required}")
        endif()
    endforeach()

    file(SHA256 "${LOADER_FILE}" loader_hash)
    string(TOUPPER "${loader_hash}" loader_hash)
    set(expected_loader_hash "D5A059AA467A7A7127C8F6169F79FA63FF0F55986EE9EB2FD9A281BEBF2AA2E6")
    if(NOT loader_hash STREQUAL expected_loader_hash)
        message(FATAL_ERROR
            "Refusing to package unverified Ultimate ASI Loader: ${loader_hash}")
    endif()

    # Amnesia.exe imports WININET.dll rather than DINPUT8.dll. Ultimate ASI
    # Loader supports both proxy names, so the verified Win32 binary is packaged
    # under the name that this executable will actually load.
    file(COPY_FILE "${LOADER_FILE}" "${OUTPUT_DIR}/wininet.dll" ONLY_IF_DIFFERENT)
    file(COPY_FILE "${NOTICE_FILE}" "${OUTPUT_DIR}/THIRD-PARTY-NOTICES.txt"
         ONLY_IF_DIFFERENT)
    list(APPEND package_files "wininet.dll" "THIRD-PARTY-NOTICES.txt")
elseif(NOT MODE STREQUAL "plugin-only")
    message(FATAL_ERROR "Unknown package mode: ${MODE}")
endif()

set(checksums "")
foreach(file IN LISTS package_files)
    file(SHA256 "${OUTPUT_DIR}/${file}" hash)
    string(TOUPPER "${hash}" hash)
    string(APPEND checksums "${hash}  ${file}\n")
endforeach()
file(WRITE "${OUTPUT_DIR}/SHA256SUMS.txt" "${checksums}")

get_filename_component(archive_dir "${ARCHIVE_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${archive_dir}")
file(REMOVE "${ARCHIVE_FILE}")
list(APPEND package_files "SHA256SUMS.txt")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E tar cf "${ARCHIVE_FILE}" --format=zip -- ${package_files}
    WORKING_DIRECTORY "${OUTPUT_DIR}"
    COMMAND_ERROR_IS_FATAL ANY
)

message(STATUS "Packaged ${MODE} release: ${ARCHIVE_FILE}")

