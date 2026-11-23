cmake_minimum_required(VERSION 3.16)

# Optional vcpkg toolchain. Must be set BEFORE project().
if(NOT CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

project(IPInventoryApi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------
# Threads
# ---------------------------------------------------------------
find_package(Threads REQUIRED)

# ---------------------------------------------------------------
# Standalone asio: system package first, pinned download as fallback
# ---------------------------------------------------------------
find_path(ASIO_INCLUDE_DIR asio.hpp)
if(NOT ASIO_INCLUDE_DIR)
    FetchContent_Declare(asio
        URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-28-2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(asio)
    set(ASIO_INCLUDE_DIR "${asio_SOURCE_DIR}/asio/include")
endif()

# ---------------------------------------------------------------
# Crow (header-only, pinned release). Its own CMake is not used
# (SOURCE_SUBDIR points to a directory that does not exist), only
# the headers are added to the include path.
# ---------------------------------------------------------------
FetchContent_Declare(crow
    URL https://github.com/CrowCpp/Crow/archive/refs/tags/v1.2.0.tar.gz
    SOURCE_SUBDIR no-cmake-here
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(crow)

# ---------------------------------------------------------------
# PostgreSQL client library (soci-postgresql.h includes libpq-fe.h)
# ---------------------------------------------------------------
find_package(PostgreSQL REQUIRED)

# ---------------------------------------------------------------
# SOCI: use a CMake config if the package ships one, otherwise
# look for headers and libraries directly (Ubuntu libsoci-dev).
# ---------------------------------------------------------------
set(SOCI_LIBS "")
set(SOCI_INCLUDE_DIRS "")

find_package(SOCI CONFIG QUIET)
foreach(core_name SOCI::soci_core SOCI::soci_core_shared SOCI::soci_core_static)
    if(NOT SOCI_LIBS AND TARGET ${core_name})
        set(SOCI_LIBS ${core_name})
    endif()
endforeach()
if(SOCI_LIBS)
    set(_soci_pg "")
    foreach(pg_name SOCI::soci_postgresql SOCI::soci_postgresql_shared SOCI::soci_postgresql_static)
        if(NOT _soci_pg AND TARGET ${pg_name})
            set(_soci_pg ${pg_name})
        endif()
    endforeach()
    if(_soci_pg)
        list(APPEND SOCI_LIBS ${_soci_pg})
    else()
        set(SOCI_LIBS "")
    endif()
endif()

if(NOT SOCI_LIBS)
    message(STATUS "SOCI CMake targets not found, searching for headers and libraries")
    find_path(SOCI_INCLUDE_DIR soci/soci.h REQUIRED)
    find_path(SOCI_PG_INCLUDE_DIR soci/postgresql/soci-postgresql.h REQUIRED)
    find_library(SOCI_CORE_LIB NAMES soci_core REQUIRED)
    find_library(SOCI_PG_LIB NAMES soci_postgresql REQUIRED)
    set(SOCI_LIBS ${SOCI_PG_LIB} ${SOCI_CORE_LIB})
    set(SOCI_INCLUDE_DIRS ${SOCI_INCLUDE_DIR} ${SOCI_INCLUDE_DIR}/soci ${SOCI_PG_INCLUDE_DIR})
endif()

# ---------------------------------------------------------------
# Executable
# ---------------------------------------------------------------
file(GLOB SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(IPInventoryApi ${SOURCES})

target_include_directories(IPInventoryApi SYSTEM PRIVATE
    ${crow_SOURCE_DIR}/include
    ${ASIO_INCLUDE_DIR}
    ${SOCI_INCLUDE_DIRS}
)

target_compile_definitions(IPInventoryApi PRIVATE ASIO_STANDALONE)

target_link_libraries(IPInventoryApi PRIVATE
    ${SOCI_LIBS}
    PostgreSQL::PostgreSQL
    Threads::Threads
)
