# CMakeList.txt: проект CMake для firmware_utils; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#
cmake_minimum_required (VERSION 3.8)

project ("firmware_utils")

# Добавьте источник в исполняемый файл этого проекта.
add_executable (firmware_utils "firmware_utils.cpp" "firmware_utils.h")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET firmware_utils PROPERTY CXX_STANDARD 20)
endif()

# TODO: Добавьте тесты и целевые объекты, если это необходимо.
