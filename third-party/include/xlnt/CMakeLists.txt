cmake_minimum_required(VERSION 3.8)
project(xlnt VERSION 1.5.0 LANGUAGES C CXX)

# The actual build logic is centralized in cmake/build-xlnt.cmake
include(${CMAKE_SOURCE_DIR}/cmake/build-xlnt.cmake)
