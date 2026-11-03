cmake_minimum_required(VERSION 3.14)
project(GDriveSyncUnitTests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ZLIB REQUIRED)

add_executable(run_unit_tests
    test_main.cpp
    test_timestamp.cpp
    test_crc32_zip.cpp
    test_sync_decision.cpp
    test_rollback.cpp
    ../../source/ZipHelper.cpp
)

target_include_directories(run_unit_tests PRIVATE . ../../source ${ZLIB_INCLUDE_DIRS})
target_link_libraries(run_unit_tests PRIVATE ZLIB::ZLIB)

enable_testing()
add_test(NAME AllUnitTests COMMAND run_unit_tests)
