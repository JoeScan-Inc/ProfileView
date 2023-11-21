/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "imgui.h"
#include "implot.h"
#include "joescan_pinchot.h"
#include "jsScanApplication.hpp"
#include <vector>
#include <iostream>
#include <fstream>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include <stdio.h>
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include <string>
#include <sstream>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

#if 0
#include "json.hpp"

static void apply_alignment_json(jsScanHead &scan_head)
{
  std::string file_name = std::to_string(serial_number) + ".json";
  std::ifstream fstream(file_name);

  try {
    using nlohmann::json;

    std::stringstream ss;
    ss << fstream.rdbuf();
    auto j = json::parse(ss.str());

    int32_t orientation = j["Orientation"];
    bool is_cable_downstream = (0 == orientation) ? false : true;

    for (auto& e : j["Alignment"]) {
      std::cout << e["Laser"] << std::endl;
      int32_t laser_id = e["Laser"]["Id"];
      double roll = e["Laser"]["RollDeg"];
      double shift_x = e["Laser"]["ShiftX"];
      double shift_y = e["Laser"]["ShiftY"];
      std::cout << id << std::endl;
      std::cout << roll << std::endl;
      std::cout << shift_x << std::endl;
      std::cout << shift_y << std::endl;

      r = jsScanHeadSetAlignmentLaser(scan_head,
                                      (jsLaser)laser_id,
                                      roll,
                                      shift_x,
                                      shift_y);
      if (0 > r) {
        throw std::runtime_error("failed to set alignment");
      }
    }
  } catch (std::exception& e) {
    // TODO: handle JSON exception
  }
}
#endif

static void glfw_error_callback(int error, const char* description)
{
  fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

int main(int argc, char* argv[])
{
  const int kMaxElementCount = 8;
  double x_data[kMaxElementCount][JS_PROFILE_DATA_LEN];
  double y_data[kMaxElementCount][JS_PROFILE_DATA_LEN];
  int data_length[kMaxElementCount] = { 0 };
  int laser_on_time_us[kMaxElementCount];
  bool is_element_enabled[kMaxElementCount];
  bool is_mode_camera = false;
  GLFWwindow* window = nullptr;
  uint32_t serial_number;
  int32_t r = 0;

  for (int i = 0; i < kMaxElementCount; ++i) {
    is_element_enabled[i] = true;
  }

  if (2 != argc) {
    std::cout << "Usage: " << argv[0] << " SERIAL" << std::endl;
    return 1;
  }

  serial_number = strtoul(argv[1], NULL, 0);

  try {
    joescan::ScanApplication app;
    jsScanHead scan_head;
    jsProfile profile;

    app.SetSerialNumber(serial_number);
    app.Connect();
    app.SetThreshold(80);
    app.SetLaserOn(500, 100, 2000);
    app.SetWindow(40.0, -40.0, -40.0, 40.0);
    app.Configure();
    app.ConfigureDistinctElementPhaseTable();
    app.StartScanning();

    scan_head = app.GetScanHeads()[0];
    jsScanHeadCapabilities cap;
    r = jsScanHeadGetCapabilities(scan_head, &cap);
    if (0 > r) {
      throw joescan::ApiError("jsScanHeadGetCapabilities failed", r);
    }

    is_mode_camera = (1 == cap.num_lasers) ? true : false;
    uint32_t element_count = (is_mode_camera) ?
                             cap.num_cameras :
                             cap.num_lasers;

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
      return 1;
    }

    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    window = glfwCreateWindow(1920,
                              1200,
                              "JoeScan JS50 ScanGui Example",
                              NULL,
                              NULL);
    if (nullptr == window) {
      return 1;
    }

    //glfwMaximizeWindow(window);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL2_Init();

    // Our state
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    while (!glfwWindowShouldClose(window)) {
      if (!glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
        continue;
      }

      glfwPollEvents();
      // Start the Dear ImGui frame
      ImGui_ImplOpenGL2_NewFrame();
      ImGui_ImplGlfw_NewFrame();
      ImGui::NewFrame();

      static float f = 0.0f;
      static int counter = 0;
#ifdef IMGUI_HAS_VIEWPORTV
      ImGuiViewport* viewport = ImGui::GetMainViewport();
      ImGui::SetNextWindowPos(viewport->GetWorkPos());
      ImGui::SetNextWindowSize(viewport->GetWorkSize());
      ImGui::SetNextWindowViewport(viewport->ID);
#else
      ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
      ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
#endif
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
      bool open = true;
      ImGui::Begin("ScanData",
                   &open,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);

      char buf[64];
      for (uint32_t i = 0; i < element_count; i++) {
        if (is_mode_camera) {
          sprintf(buf, "Camera %d", i + 1);
        } else {
          sprintf(buf, "Laser %d]", i + 1);
        }

        ImGui::Checkbox(buf, &is_element_enabled[i]);
        ImGui::SameLine();
      }

      ImGui::Text("Encoder = %lu", (int64_t) profile.encoder_values[0]);

      auto is_plot_sucess = ImPlot::BeginPlot("Profile Plot",
                                              "X [inches]",
                                              "Y [inches]",
                                              ImVec2(-1, -1),
                                              ImPlotFlags_Equal);
      if (!is_plot_sucess) {
        continue;
      }

      ImPlot::SetupAxesLimits(-50.0, 50.0, -50.0, 50.0);
      ImPlot::SetupFinish();

      r = jsScanHeadWaitUntilProfilesAvailable(scan_head, 1, 10000);
      if (0 > r) {
        throw joescan::ApiError(
          "jsScanHeadWaitUntilProfilesAvailable failed", r);
      }

      uint32_t profiles_available = r;
      for (uint32_t k = 0; k < profiles_available; k++) {
        r = jsScanHeadGetProfiles(scan_head, &profile, 1);
        if (0 > r) {
          throw joescan::ApiError("jsScanHeadGetProfiles failed", r);
        }

        uint32_t idx = (is_mode_camera) ?
                       ((uint32_t) profile.camera) - 1 :
                       ((uint32_t) profile.laser) - 1;

        data_length[idx] = profile.data_len;
        laser_on_time_us[idx] = profile.laser_on_time_us;

        // Worst case, we redraw laser1 data
        for (uint32_t n = 0; n < profile.data_len; n++) {
          x_data[idx][n] = profile.data[n].x / 1000.0;
          y_data[idx][n] = profile.data[n].y / 1000.0;
        }
      }

      char legend[32];
      for (uint32_t i = 0; i < element_count; i++)
      {
        ImPlot::SetNextMarkerStyle(ImPlotMarker_Square,
                                   1,
                                   ImPlot::GetColormapColor(i),
                                   IMPLOT_AUTO,
                                   ImPlot::GetColormapColor(i));
        if (is_mode_camera) {
          sprintf(legend, "Camera %d [%duS]", i + 1, laser_on_time_us[i]);
        } else {
          sprintf(legend, "Laser %d [%duS]", i + 1, laser_on_time_us[i]);
        }

        if (is_element_enabled[i]) {
          ImPlot::PlotScatter(legend, x_data[i], y_data[i], data_length[i]);
        }
      }

      ImPlot::EndPlot();
      ImGui::End();
      ImGui::PopStyleVar();
      ImGui::Render();
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      glViewport(0, 0, display_w, display_h);
      glClearColor(clear_color.x * clear_color.w,
                   clear_color.y * clear_color.w,
                   clear_color.z * clear_color.w,
                   clear_color.w);
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
      glfwMakeContextCurrent(window);
      glfwSwapBuffers(window);
    }

    app.StopScanning();

  } catch (joescan::ApiError &e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    r = 1;

    const char *err_str = nullptr;
    jsError err = e.return_code();
    if (JS_ERROR_NONE != err) {
      jsGetError(err, &err_str);
      std::cout << "jsError (" << err << "): " << err_str << std::endl;
    }
  }

  // Cleanup
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}
