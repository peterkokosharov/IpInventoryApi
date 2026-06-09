If you're starting a new C++ REST API project today, the frameworks most commonly recommended and actively used are:

Drogon
Probably the most popular modern choice for high-performance REST services.
Asynchronous, cross-platform, built-in ORM, WebSocket support.
Frequently cited as one of the fastest C++ web frameworks.
Crow
Very lightweight and easy to learn.
Flask-like routing style.
Good for small to medium REST APIs and microservices.
Pistache
REST-focused framework with a clean API.
Widely known in the C++ community.
Suitable for HTTP servers and REST endpoints.

Other notable options:

Boost.Beast — very powerful but lower-level.
oat++ — productive framework with OpenAPI support.
cpp-httplib — extremely simple and lightweight.
POCO — mature enterprise-oriented library set.
Recommendation by use case
Need	Recommended
High-performance production REST API	Drogon
Easy Flask-like development	Crow
Pure REST-focused framework	Pistache
Maximum control / custom networking	Boost.Beast
Fast development with OpenAPI support	oat++

For most new projects in 2025–2026, I'd choose Drogon unless you specifically want a very lightweight framework, in which case Crow is an excellent alternative.

my notes:
so drogon support excelent code, but vcpkg have problem with cmd
vcpkg install drogon[cli,postgres,orm]:x64-windows
my attempt make this task with drogon can be seen here https://github.com/peterkokosharov/ip-inventory
as I understood, problem is in last version with encoings and localization of developer PC. Many korean, japanese developer complaint that they cannot install drogon vcpkg

so, I choose crow framework

developer/building enviroment
Visual Studio Community 2019
vcpkg - last version
cmake
Windows SDK 10
postgresql 18

If you're using vcpkg and want to build a C++ REST service with Crow and PostgreSQL, here are the typical steps.

1. Install vcpkg

If you don't already have it:

git clone https://github.com/microsoft/vcpkg.git
cd vcpkg

# Linux/macOS
./bootstrap-vcpkg.sh

# Windows
bootstrap-vcpkg.bat
2. Install Crow

Search available packages:

vcpkg search crow

Install Crow:

vcpkg install crow

Or for x64 Linux:

vcpkg install crow:x64-linux

Or for Windows:

vcpkg install crow:x64-windows
3. Install PostgreSQL client library

Most C++ PostgreSQL libraries depend on libpq.

Install:

vcpkg install libpq

or:

vcpkg install libpq:x64-linux
4. Install a modern PostgreSQL C++ wrapper (recommended)
Option A: libpqxx (recommended)
vcpkg install libpqxx

This automatically pulls libpq.

Example:

#include <pqxx/pqxx>

pqxx::connection conn(
    "host=localhost port=5432 dbname=testdb user=postgres password=secret");

pqxx::work tx(conn);

auto result = tx.exec(
    "SELECT id, name FROM users");

for (auto row : result)
{
    std::cout << row["id"].as<int>()
              << " "
              << row["name"].as<std::string>()
              << std::endl;
}
5. CMake integration

Install dependencies:

vcpkg install crow libpqxx

Configure CMake:

cmake_minimum_required(VERSION 3.20)
project(RestApi)

find_package(Crow CONFIG REQUIRED)
find_package(libpqxx CONFIG REQUIRED)

add_executable(RestApi main.cpp)

target_link_libraries(RestApi PRIVATE
    Crow::Crow
    libpqxx::pqxx
)

Configure build:

cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build