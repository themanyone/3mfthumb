cmake_minimum_required(VERSION 3.15)

project(3mfthumb
  VERSION
    1.1
  DESCRIPTION
    "The premier .3mf thumbnail viewer for file managers in Linux and Windows."
  LANGUAGES
    CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
  # Windows: use the bundled lib3mf SDK (headers + import lib) and copy the
  # runtime DLL next to the exe so it is found at run time.
  set(LIB3MF_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib3mf_sdk")
  set(CMAKE_CURRENT_BINDING_DIR "${LIB3MF_SDK_DIR}/Bindings/Cpp")
  set(CMAKE_CURRENT_LIB_DIR "${LIB3MF_SDK_DIR}/Lib")

  add_executable(3mfthumb 3mfthumb.cpp)
  target_include_directories(3mfthumb PRIVATE "${CMAKE_CURRENT_BINDING_DIR}")
  target_link_directories(3mfthumb PRIVATE "${CMAKE_CURRENT_LIB_DIR}")
  target_link_libraries(3mfthumb PRIVATE 3mf zip z)

  add_custom_command(TARGET 3mfthumb POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${LIB3MF_SDK_DIR}/Bin/lib3mf.dll"
            "$<TARGET_FILE_DIR:3mfthumb>")
else()
  # Linux/macOS: locate the distro lib3mf via pkg-config.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIB3MF REQUIRED IMPORTED_TARGET lib3mf)
  add_executable(3mfthumb 3mfthumb.cpp)
  target_link_libraries(3mfthumb PRIVATE PkgConfig::LIB3MF)
endif()
