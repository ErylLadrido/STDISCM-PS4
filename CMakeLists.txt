# CMakeList.txt : CMake project for Qt GUI OCR Client
cmake_minimum_required(VERSION 3.8)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

# Allow AUTOMOC/AUTOUIC to process generated files
if (POLICY CMP0071)
  cmake_policy(SET CMP0071 NEW)
endif()

project("Qt GUI" VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Prioritize vcpkg packages over system packages
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Find gRPC and Protobuf
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Proto file
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/ocr_service.proto")

# Check proto file exists
if(NOT EXISTS ${PROTO_FILE})
    message(FATAL_ERROR "Proto file not found: ${PROTO_FILE}")
endif()

# Set output directory for generated files
set(PROTO_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_BINARY_DIR})

# Get protoc and grpc plugin
get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION_RELEASE)
if(NOT PROTOC_EXECUTABLE)
    get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION_DEBUG)
endif()
if(NOT PROTOC_EXECUTABLE)
    get_target_property(PROTOC_EXECUTABLE protobuf::protoc IMPORTED_LOCATION)
endif()

get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_RELEASE)
if(NOT GRPC_CPP_PLUGIN)
    get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION_DEBUG)
endif()
if(NOT GRPC_CPP_PLUGIN)
    get_target_property(GRPC_CPP_PLUGIN gRPC::grpc_cpp_plugin IMPORTED_LOCATION)
endif()

message(STATUS "Protoc: ${PROTOC_EXECUTABLE}")
message(STATUS "gRPC plugin: ${GRPC_CPP_PLUGIN}")

# Convert to native paths for Windows
file(TO_NATIVE_PATH "${GRPC_CPP_PLUGIN}" GRPC_CPP_PLUGIN_NATIVE)

# Generated source files
set(PROTO_SRCS "${PROTO_BINARY_DIR}/ocr_service.pb.cc")
set(PROTO_HDRS "${PROTO_BINARY_DIR}/ocr_service.pb.h")
set(GRPC_SRCS "${PROTO_BINARY_DIR}/ocr_service.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_BINARY_DIR}/ocr_service.grpc.pb.h")

# Custom command to generate protobuf files
add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROTO_BINARY_DIR}
    COMMAND ${PROTOC_EXECUTABLE}
    ARGS --grpc_out=${PROTO_BINARY_DIR}
         --cpp_out=${PROTO_BINARY_DIR}
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating protobuf and gRPC files"
    VERBATIM
)

# Force generation at configure time for IntelliSense
execute_process(
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROTO_BINARY_DIR}"
)
execute_process(
    COMMAND "${PROTOC_EXECUTABLE}"
    --grpc_out=${PROTO_BINARY_DIR}
    --cpp_out=${PROTO_BINARY_DIR}
    -I${CMAKE_CURRENT_SOURCE_DIR}/proto
    --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN}
    ${PROTO_FILE}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RESULT_VARIABLE PROTO_GENERATION_RESULT
    OUTPUT_VARIABLE PROTO_GENERATION_OUTPUT
    ERROR_VARIABLE PROTO_GENERATION_ERROR
)

if(NOT PROTO_GENERATION_RESULT EQUAL 0)
    message(WARNING "Protobuf generation failed: ${PROTO_GENERATION_ERROR}")
else()
    message(STATUS "Successfully generated protobuf files")
endif()

# Mark generated files to skip Qt's AUTOMOC/AUTOUIC
set_source_files_properties(
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${GRPC_SRCS}
    ${GRPC_HDRS}
    PROPERTIES
    SKIP_AUTOMOC ON
    SKIP_AUTOUIC ON
    GENERATED TRUE
)

# Create proto library
add_library(ocr_proto STATIC
    ${PROTO_SRCS}
    ${PROTO_HDRS}
    ${GRPC_SRCS}
    ${GRPC_HDRS}
)

target_link_libraries(ocr_proto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc++
)

target_include_directories(ocr_proto PUBLIC
    ${PROTO_BINARY_DIR}
)

# Main executable
add_executable(OCRClient
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/OCRClient.cpp
    src/OCRClient.h
)

target_link_libraries(OCRClient
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        ocr_proto
        gRPC::grpc++
        protobuf::libprotobuf
)

target_include_directories(OCRClient PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROTO_BINARY_DIR}
)

# Ensure OCRClient can see the generated headers
add_dependencies(OCRClient ocr_proto)


# OCR Server executable
add_executable(OCRServer
    src/OCRServer.cpp
    src/OCRService.cpp
    src/OCRProcessor.cpp
    src/ThreadPool.cpp
)

target_link_libraries(OCRServer
    PRIVATE
        ocr_proto
        gRPC::grpc++
        gRPC::grpc
        protobuf::libprotobuf
)

target_include_directories(OCRServer PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${PROTO_BINARY_DIR}
)

# Find Tesseract and Leptonica for server (macOS specific)
find_library(TESSERACT_LIB NAMES tesseract)
find_library(LEPTONICA_LIB NAMES leptonica)

if(TESSERACT_LIB AND LEPTONICA_LIB)
    target_link_libraries(OCRServer PRIVATE ${TESSERACT_LIB} ${LEPTONICA_LIB})
else()
    message(WARNING "Tesseract and/or Leptonica not found. Server may not build correctly.")
endif()

# Include directories for Tesseract/Leptonica (adjust paths as needed)
target_include_directories(OCRServer PRIVATE
    /opt/homebrew/include
    /usr/local/include
)

# Ensure OCRServer can see the generated headers
add_dependencies(OCRServer ocr_proto)
# Ensure OCRServer can see the generated headers
add_dependencies(OCRServer ocr_proto)

# Set the startup project for Visual Studio
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT OCRClient)

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET OCRClient PROPERTY CXX_STANDARD 20)
endif()