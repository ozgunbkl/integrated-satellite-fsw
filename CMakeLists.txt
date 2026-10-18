cmake_minimum_required(VERSION 3.5)

# Shared mission contracts live in the header-only components/fsw_common target.
# Runtime services depend on it; it must not depend on those services.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Integrated_Satellite_FSW)
