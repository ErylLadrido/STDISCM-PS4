cmake_minimum_required(VERSION 3.16)

project(ocr_pipeline VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

include_directories(
    /opt/homebrew/Cellar/leptonica/1.86.0/include
    /opt/homebrew/Cellar/tesseract/5.5.1/include
)

link_directories(
    /opt/homebrew/Cellar/leptonica/1.86.0/lib
    /opt/homebrew/Cellar/tesseract/5.5.1/lib
)

add_executable(ocr_pipeline LADRIDO_PS3.cpp)

target_link_libraries(ocr_pipeline PRIVATE 
    tesseract 
    leptonica
    Threads::Threads
)