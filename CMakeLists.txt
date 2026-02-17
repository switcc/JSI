cmake_minimum_required(VERSION 3.16)
project(JSI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(JSI_ENABLE_QUICKJS  "Build the QuickJS backend"               ON)
option(JSI_ENABLE_V8       "Build the V8 backend"                    ON)
option(JSI_BUILD_EXAMPLES  "Build example programs"                  ON)
option(JSI_FETCH_QUICKJS   "Download QuickJS via FetchContent"       ON)

# User-supplied source directories (override auto-detection)
set(QUICKJS_SOURCE_DIR "" CACHE PATH "Path to QuickJS source tree (containing quickjs.h)")
set(V8_ROOT            "" CACHE PATH "Path to V8 installation root")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------------------------------------------------------------------
# Core library (always built)
# ---------------------------------------------------------------------------
add_library(jsi src/runtime.cpp)
target_include_directories(jsi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_include_directories(jsi PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(JSI_BACKENDS_FOUND 0)

# ---------------------------------------------------------------------------
# QuickJS backend
# ---------------------------------------------------------------------------
if(JSI_ENABLE_QUICKJS)
    set(_quickjs_found FALSE)

    # Strategy 1: User-provided source directory
    if(QUICKJS_SOURCE_DIR AND EXISTS "${QUICKJS_SOURCE_DIR}/quickjs.h")
        message(STATUS "JSI: Using QuickJS from QUICKJS_SOURCE_DIR=${QUICKJS_SOURCE_DIR}")
        set(_qjs_src_dir "${QUICKJS_SOURCE_DIR}")
        set(_quickjs_found TRUE)
    endif()

    # Strategy 2: Vendored in third_party/quickjs
    if(NOT _quickjs_found AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs/quickjs.h")
        message(STATUS "JSI: Using vendored QuickJS from third_party/quickjs")
        set(_qjs_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/quickjs")
        set(_quickjs_found TRUE)
    endif()

    # Strategy 3: System-installed (look for header + library)
    if(NOT _quickjs_found)
        find_path(_qjs_inc NAMES quickjs.h
            HINTS /usr/include /usr/local/include /usr/include/quickjs)
        find_library(_qjs_lib NAMES quickjs
            HINTS /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu)
        if(_qjs_inc AND _qjs_lib)
            message(STATUS "JSI: Found system QuickJS: ${_qjs_inc}, ${_qjs_lib}")
            # Use the system library directly
            target_sources(jsi PRIVATE
                src/quickjs/quickjs_bridge.c
                src/quickjs/quickjs_engine.cpp
            )
            target_include_directories(jsi PRIVATE "${_qjs_inc}")
            target_link_libraries(jsi PRIVATE "${_qjs_lib}")
            target_compile_definitions(jsi PUBLIC JSI_HAS_QUICKJS=1)
            set(_quickjs_found TRUE)
            set(_quickjs_system TRUE)
            math(EXPR JSI_BACKENDS_FOUND "${JSI_BACKENDS_FOUND} + 1")
        endif()
    endif()

    # Strategy 4: FetchContent via git clone
    if(NOT _quickjs_found AND JSI_FETCH_QUICKJS)
        message(STATUS "JSI: Attempting to fetch QuickJS via FetchContent...")
        include(FetchContent)
        FetchContent_Declare(
            quickjs
            GIT_REPOSITORY https://github.com/bellard/quickjs.git
            GIT_TAG        master
            GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(quickjs)
        if(EXISTS "${quickjs_SOURCE_DIR}/quickjs.h")
            set(_qjs_src_dir "${quickjs_SOURCE_DIR}")
            set(_quickjs_found TRUE)
        endif()
    endif()

    # Build QuickJS from source if we have a source directory (not system lib)
    if(_quickjs_found AND NOT _quickjs_system)
        # Determine which source files to compile (bellard/quickjs layout)
        if(EXISTS "${_qjs_src_dir}/quickjs.c")
            set(_qjs_sources "${_qjs_src_dir}/quickjs.c")
            # bellard/quickjs requires these additional sources
            foreach(_extra cutils.c libregexp.c libunicode.c)
                if(EXISTS "${_qjs_src_dir}/${_extra}")
                    list(APPEND _qjs_sources "${_qjs_src_dir}/${_extra}")
                endif()
            endforeach()
        elseif(EXISTS "${_qjs_src_dir}/src/quickjs.c")
            set(_qjs_sources "${_qjs_src_dir}/src/quickjs.c")
        else()
            message(WARNING "JSI: quickjs.h found but quickjs.c not found in ${_qjs_src_dir}")
            set(_quickjs_found FALSE)
        endif()

        if(_quickjs_found)
            add_library(quickjs_lib STATIC ${_qjs_sources})
            target_include_directories(quickjs_lib PUBLIC "${_qjs_src_dir}")
            target_compile_definitions(quickjs_lib PRIVATE
                CONFIG_VERSION="2024-02"
                _GNU_SOURCE
            )
            if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
                target_compile_options(quickjs_lib PRIVATE -w)
            endif()
            target_link_libraries(quickjs_lib PRIVATE m)

            # Bridge is compiled as C (avoids JSValue compound-literal
            # conflicts between quickjs.h and C++)
            target_sources(jsi PRIVATE
                src/quickjs/quickjs_bridge.c
                src/quickjs/quickjs_engine.cpp
            )
            target_link_libraries(jsi PRIVATE quickjs_lib)
            target_compile_definitions(jsi PUBLIC JSI_HAS_QUICKJS=1)
            math(EXPR JSI_BACKENDS_FOUND "${JSI_BACKENDS_FOUND} + 1")
        endif()
    endif()

    if(_quickjs_found)
        message(STATUS "JSI: QuickJS backend ENABLED")
    else()
        message(STATUS "JSI: QuickJS backend disabled (not found). Options:\n"
                       "  - Place QuickJS source in third_party/quickjs/\n"
                       "  - Set -DQUICKJS_SOURCE_DIR=/path/to/quickjs\n"
                       "  - Install QuickJS system-wide\n"
                       "  - Enable JSI_FETCH_QUICKJS with network access")
    endif()
endif()

# ---------------------------------------------------------------------------
# V8 backend
# ---------------------------------------------------------------------------
if(JSI_ENABLE_V8)
    find_package(V8 QUIET)

    if(V8_FOUND)
        target_sources(jsi PRIVATE src/v8/v8_engine.cpp)
        target_include_directories(jsi PRIVATE ${V8_INCLUDE_DIRS})
        target_link_libraries(jsi PRIVATE ${V8_LIBRARIES})
        target_compile_definitions(jsi PUBLIC JSI_HAS_V8=1)
        math(EXPR JSI_BACKENDS_FOUND "${JSI_BACKENDS_FOUND} + 1")
        message(STATUS "JSI: V8 backend ENABLED (${V8_INCLUDE_DIRS})")
    else()
        message(STATUS "JSI: V8 backend disabled (not found). "
                       "Set -DV8_ROOT=/path/to/v8 to enable.")
    endif()
endif()

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
if(JSI_BACKENDS_FOUND EQUAL 0)
    message(WARNING "JSI: No JS engine backends found! "
                    "The library will compile but cannot create any engine.\n"
                    "Run ./setup.sh to download dependencies.")
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(JSI_BUILD_EXAMPLES)
    add_executable(jsi_example examples/main.cpp)
    target_link_libraries(jsi_example PRIVATE jsi)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS jsi ARCHIVE DESTINATION lib)
install(DIRECTORY include/jsi DESTINATION include)
