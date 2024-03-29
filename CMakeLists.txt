cmake_minimum_required (VERSION 3.20)
project(js50-profile-view)

set(CMAKE_VERBOSE_MAKEFILE ON)

if(NOT DEFINED PINCHOT_API_ROOT_DIR)
  message(FATAL_ERROR 
  "PINCHOT_API_ROOT_DIR not defined!\n"
  "please set using -PINCHOT_API_ROOT_DIR=")
else()
  if(PINCHOT_API_ROOT_DIR STREQUAL "")
    message(FATAL_ERROR "PINCHOT_API_ROOT_DIR is empty, please provide path to Pinchot C API root directory")
  endif()
endif()

set(PINCHOT_API_ROOT_DIR "" CACHE FILEPATH "this is a dumb cmake command")
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(IMGUI_SOURCE_DIR ${SOURCE_DIR}/imgui)
set(GLFW_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glfw3/glfw-3.3.8)

if(WIN32)
  set(OpenGL_GL_PREFERENCE GLVND)
  find_package(OpenGL)
  set(GLFW_LIBRARY ${CMAKE_CURRENT_BINARY_DIR}/glfw-build/src/$(ConfigurationName)/glfw3.lib)
  set(LINK_LIBS
      ${GLFW_LIBRARY}
      ${OPENGL_LIBRARY})
  message(${GLFW_LIBRARY})
endif()

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
endif()

file(GLOB PROJECT_SOURCES
  ${SOURCE_DIR}/*.cpp
  ${IMGUI_SOURCE_DIR}/*.cpp)

include_directories(${IMGUI_SOURCE_DIR})
include_directories(${PINCHOT_API_ROOT_DIR}/examples)
include_directories(${GLFW_DIR}/include)

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF)
add_subdirectory(${GLFW_DIR} ${CMAKE_CURRENT_BINARY_DIR}/glfw-build)

add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCES})
target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC ${LINK_LIBS})

list(APPEND CMAKE_MODULE_PATH ${PINCHOT_API_ROOT_DIR})
include(PinchotBuildApplication RESULT_VARIABLE HAVE_PINCHOT_BUILD_APP)

#Could get rid of this since include already throws an error if it cant find the cmake file but adds some clarity
if(NOT HAVE_PINCHOT_BUILD_APP)
  message("PinchotBuildApplication was not found, check PINCHOT_API_ROOT_DIR define")
endif()
