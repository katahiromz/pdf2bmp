# CMakeLists.txt --- CMake project settings
##############################################################################

# CMake minimum version
cmake_minimum_required(VERSION 3.0)

# project name and languages
project(pdf2bmp CXX)

# Use pdfium library (Please specify "PDFium_DIR" environment variable)
find_package(PDFium REQUIRED)

# Unicode
add_definitions(-DUNICODE -D_UNICODE)

# pdf2bmp.exe
add_executable(pdf2bmp pdf2bmp.cpp SaveBitmapToFile.cpp)
target_include_directories(pdf2bmp PRIVATE "${PDFium_INCLUDE_DIR}")
target_link_libraries(pdf2bmp PRIVATE pdfium shlwapi)
