# FindV8.cmake – Locate V8 headers and libraries
#
# This module defines:
#   V8_FOUND        – True if V8 was found
#   V8_INCLUDE_DIRS – Include directories
#   V8_LIBRARIES    – Libraries to link
#
# Search hints:
#   V8_ROOT – root of a V8 installation

find_path(V8_INCLUDE_DIR
    NAMES v8.h
    HINTS
        ${V8_ROOT}/include
        /usr/include/v8
        /usr/local/include/v8
        /usr/include
        /usr/local/include
        $ENV{V8_ROOT}/include
)

find_library(V8_LIBRARY
    NAMES v8 v8_monolith
    HINTS
        ${V8_ROOT}/lib
        ${V8_ROOT}/out.gn/x64.release/obj
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        $ENV{V8_ROOT}/lib
)

find_library(V8_PLATFORM_LIBRARY
    NAMES v8_libplatform
    HINTS
        ${V8_ROOT}/lib
        ${V8_ROOT}/out.gn/x64.release/obj
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/local/lib
        $ENV{V8_ROOT}/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(V8
    REQUIRED_VARS V8_INCLUDE_DIR V8_LIBRARY
)

if(V8_FOUND)
    set(V8_INCLUDE_DIRS ${V8_INCLUDE_DIR})
    set(V8_LIBRARIES ${V8_LIBRARY})
    if(V8_PLATFORM_LIBRARY)
        list(APPEND V8_LIBRARIES ${V8_PLATFORM_LIBRARY})
    endif()
endif()
