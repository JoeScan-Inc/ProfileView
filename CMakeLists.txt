cmake_minimum_required (VERSION 3.1)
project(js50-profile-view)

cmake_minimum_required (VERSION 3.20)
set(CMAKE_VERBOSE_MAKEFILE ON)

if(NOT DEFINED PINCHOT_API_ROOT_DIR)
  message(FATAL_ERROR 
  "PINCHOT_API_ROOT_DIR not defined!\n"
  "please set using -PINCHOT_API_ROOT_DIR=")
endif()

set(PINCHOT_API_ROOT_DIR "" CACHE FILEPATH "this is a dumb cmake command")
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IMGUI_SOURCE_DIR ${SOURCE_DIR}/imgui)
set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glfw3/glfw-3.3.8)

if(WIN32)

endif(WIN32)

if(UNIX)
  set(OpenGL_GL_PREFERENCE GLVND)
  find_package(OpenGL)
  find_package(X11 COMPONENTS X11 Xft)
  set(GLFW_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/glfw-build/src/libglfw3.a)

  set(LINK_LIBS
      ${GLFW_LIBRARY}
      ${OPENGL_LIBRARY}
      ${X11_LIBRARIES}
      -ldl
      -lm)
endif(UNIX)

file(GLOB PROJECT_SOURCES
  ${SOURCE_DIR}/*.cpp
  ${IMGUI_SOURCE_DIR}/*.cpp)

include_directories(${IMGUI_SOURCE_DIR})
include_directories(${PINCHOT_API_ROOT_DIR}/examples)
include_directories(${GLFW_DIR}/include)

add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCES})
target_link_libraries(${CMAKE_PROJECT_NAME} ${LINK_LIBS})

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(${GLFW_DIR} ${CMAKE_CURRENT_BINARY_DIR}/glfw-build)

list(APPEND CMAKE_MODULE_PATH ${PINCHOT_API_ROOT_DIR})
include(PinchotBuildApplication)
