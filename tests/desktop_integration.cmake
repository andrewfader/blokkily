# features/open_from_desktop.feature: the desktop entry claims the type the
# MIME package defines, hands the opened file to the program, and the
# package, compiled as a desktop's update-mime-database compiles it, maps
# *.blok and a project's first bytes to that type.
#   cmake -DSOURCE=<repo> -DWORK=<dir> -DUPDATE_MIME=<exe> -DVALIDATE=<exe> -P
cmake_minimum_required(VERSION 3.25)
# `found` is a string(FIND) result, -1 when nothing was found.
function(require_found found message)
    if(found EQUAL -1)
        message(FATAL_ERROR "FAIL ${message}")
    endif()
endfunction()
function(require_success result message)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "FAIL ${message}")
    endif()
endfunction()

set(type "application/x-blokkily-project")
file(READ "${SOURCE}/resources/blokkily.desktop" desktop)
file(READ "${SOURCE}/resources/blokkily-mime.xml" package)

string(REGEX MATCH "\nExec=[^\n]*" exec "${desktop}")
string(FIND "${exec}" "%f" has_file)
require_found(${has_file} "the desktop entry passes the opened file (Exec … %f): ${exec}")
string(FIND "${desktop}" "\nMimeType=${type};" claims)
require_found(${claims} "the desktop entry claims ${type}")
string(FIND "${package}" "type=\"${type}\"" defines)
require_found(${defines} "the MIME package defines ${type}")

execute_process(COMMAND "${VALIDATE}" "${SOURCE}/resources/blokkily.desktop"
                RESULT_VARIABLE valid OUTPUT_VARIABLE said ERROR_VARIABLE said)
require_success(${valid} "desktop-file-validate accepts the entry: ${said}")

# Compile the package into a scratch database exactly as a desktop does.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/mime/packages")
configure_file("${SOURCE}/resources/blokkily-mime.xml" "${WORK}/mime/packages/blokkily.xml" COPYONLY)
execute_process(COMMAND "${UPDATE_MIME}" "${WORK}/mime"
                RESULT_VARIABLE built OUTPUT_VARIABLE said ERROR_VARIABLE said)
require_success(${built} "update-mime-database compiles the package: ${said}")
file(READ "${WORK}/mime/globs2" globs)
string(FIND "${globs}" ":${type}:*.blok" globbed)
require_found(${globbed} "*.blok maps to ${type}")
file(READ "${WORK}/mime/magic" magic HEX)
string(HEX "blokkily-project " header)
string(FIND "${magic}" "${header}" sniffed)
require_found(${sniffed} "a project's first bytes are known without its name")
file(READ "${WORK}/mime/types" types)
string(FIND "${types}" "${type}" listed)
require_found(${listed} "${type} is a known type")
message(STATUS "PASS desktop integration")
