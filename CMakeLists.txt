cmake_minimum_required(VERSION 3.16)
project(MetricsModel 
VERSION 1.0.0
LANGUAGES CXX
)

add_compile_definitions(METRICS_MODEL_VERSION="${PROJECT_VERSION}" )


set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SRC_FILES
  CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
  ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
)

add_library(MetricsModel STATIC ${SRC_FILES})

target_include_directories(MetricsModel
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ### BOOST ####
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost 1.75 REQUIRED COMPONENTS system thread)

target_include_directories(MetricsModel PRIVATE
  ${Boost_INCLUDE_DIRS}
)
target_link_libraries(MetricsModel PRIVATE
  Boost::system
  Boost::thread
)

set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
set_target_properties(MetricsModel PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ### BOOST ####