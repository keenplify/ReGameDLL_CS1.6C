cmake_minimum_required(VERSION 3.1)
project(regamedll CXX)

if (WIN32)
	message(FATAL_ERROR "CMakeLists.txt Windows platform isn't supported yet. Use msvc/ReGameDLL.sln instead it!")
endif()

add_custom_target(appversion DEPENDS
	COMMAND "${PROJECT_SOURCE_DIR}/regamedll/version/appversion.sh" "${PROJECT_SOURCE_DIR}"
)

add_subdirectory(regamedll)

include(CTest)
if (BUILD_TESTING)
	add_executable(competitive_bot_tests tests/competitive_bot_test.cpp)
	target_include_directories(competitive_bot_tests PRIVATE regamedll/dlls/bot)
	set_target_properties(competitive_bot_tests PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
	add_test(NAME competitive_bot_policy COMMAND competitive_bot_tests)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
 add_library(regamedll_query SHARED query/query_preload.cpp)
 target_compile_options(regamedll_query PRIVATE -m32)
 set_target_properties(regamedll_query PROPERTIES LINK_FLAGS "-m32" CXX_STANDARD 14)
 target_link_libraries(regamedll_query PRIVATE dl pthread)
endif()

if (BUILD_TESTING)
 add_executable(query_packet_tests tests/query_packet_test.cpp)
 set_target_properties(query_packet_tests PROPERTIES CXX_STANDARD 14)
 add_test(NAME query_packet_bounds COMMAND query_packet_tests)
endif()

if (BUILD_TESTING)
 add_executable(competitive_ping_tests tests/competitive_ping_test.cpp)
 set_target_properties(competitive_ping_tests PROPERTIES CXX_STANDARD 14)
 add_test(NAME competitive_ping_protocol COMMAND competitive_ping_tests)
endif()

if (BUILD_TESTING)
 add_executable(competitive_utility_tests tests/competitive_utility_test.cpp)
 set_target_properties(competitive_utility_tests PROPERTIES CXX_STANDARD 14)
 add_test(NAME competitive_utility_policy COMMAND competitive_utility_tests)
endif()
