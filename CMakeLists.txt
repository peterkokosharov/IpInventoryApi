cmake_minimum_required(VERSION 3.16)
project(IPInventoryApi)

set(CMAKE_CXX_STANDARD 17)

# Add source
file(GLOB SOURCES "src/*.cpp")

add_executable(IPInventoryApi ${SOURCES})

# Use vcpkg toolchain
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "")
endif()

find_package(SOCI REQUIRED)

target_link_libraries(IPInventoryApi PRIVATE
        SOCI::SOCI
        SOCI::PostgreSQL
)