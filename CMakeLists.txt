cmake_minimum_required(VERSION 3.18)

if(POLICY CMP0148)
  cmake_policy(SET CMP0148 OLD)
endif()

project(ottersec VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(GST REQUIRED gstreamer-1.0)
pkg_check_modules(GST_APP REQUIRED gstreamer-app-1.0)
pkg_check_modules(GST_ALLOC REQUIRED gstreamer-allocators-1.0)
pkg_check_modules(OPENSSL REQUIRED openssl)

find_package(Threads REQUIRED)
find_package(pybind11 CONFIG QUIET)
find_package(Protobuf REQUIRED)

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/pose_data.proto")
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILE})

add_library(ottersec SHARED
    src/api.cpp
    src/core/security_context.cpp
    src/core/session_manager.cpp
    src/core/handshake_server.cpp
    src/core/control_server.cpp
    src/pipeline/gstreamer_pipeline.cpp
    ${PROTO_SRCS}
    ${PROTO_HDRS}
)

target_include_directories(ottersec PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    ${CMAKE_CURRENT_BINARY_DIR}
    ${GST_INCLUDE_DIRS}
    ${GST_APP_INCLUDE_DIRS}
    ${GST_ALLOC_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIRS}
    ${PROTOBUF_INCLUDE_DIRS}
)

target_link_libraries(ottersec PRIVATE
    ${GST_LIBRARIES}
    ${GST_APP_LIBRARIES}
    ${GST_ALLOC_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    protobuf::libprotobuf
    Threads::Threads
)

set_target_properties(ottersec PROPERTIES
    OUTPUT_NAME "ottersec"
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(pybind11_FOUND)
    message(STATUS "Found pybind11. Building Python bindings...")
    pybind11_add_module(pyottersec src/bindings/python/pybind_module.cpp)
    
    target_include_directories(pyottersec PRIVATE include)
    target_link_libraries(pyottersec PRIVATE ottersec)
else()
    message(WARNING "pybind11 not found. Skipping Python bindings. You can still use ctypes.")
endif()

include(GNUInstallDirs)

install(TARGETS ottersec
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/ottersec/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ottersec
)
