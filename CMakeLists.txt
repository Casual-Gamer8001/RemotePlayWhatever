# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required (VERSION 3.8)

# Steam's client library is read as x86-64 code (see ClientVTableByName.h), so
# the Mac version is an Intel app; Apple Silicon Macs run it with Rosetta 2.
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64" CACHE STRING "Mac architecture: x86_64 only (Rosetta 2 runs it on Apple Silicon)")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS version the app runs on")
endif()

project ("RemotePlayWhatever" VERSION 0.2.14)

# Include sub-projects.
add_subdirectory ("RemotePlayWhatever")
