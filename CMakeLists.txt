cmake_minimum_required(VERSION 3.16)
project(MetricsModel VERSION 1.1.1 LANGUAGES CXX)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../tools/workspace.cmake")
create_target(model)

find_package(Boost 1.8 REQUIRED COMPONENTS system thread)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::system Boost::thread)
