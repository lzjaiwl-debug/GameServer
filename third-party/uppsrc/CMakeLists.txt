cmake_minimum_required(VERSION 3.8)
project(upp VERSION 1.0 LANGUAGES C CXX)

# The actual build logic is centralized in cmake/build-upp.cmake
include(${CMAKE_SOURCE_DIR}/cmake/build-upp.cmake)



