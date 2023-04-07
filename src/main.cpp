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
#include "jsCircleHough.h"
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

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

#define PI 3.14159265

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

/**
 * @brief Struct to define the center of a circle
*/
struct CircleCenter {
  double x;
  double y;
  CircleCenter() {
    x = 0.0;
    y = 0.0;
  }
};

/**
 * @brief Struct to define the fixture in the scene
*/
struct Fixture {
  double x;
  double y;
  uint32_t rotation;
  Fixture() {
    x = 0.0;
    y = 0.0;
    rotation = 0;
  }
};

static void glfw_error_callback(int error, const char* description)
{
  fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

double calculate_distance(double x, double y)
{
  double dist;

  dist = pow(x, 2) + pow(y, 2);
  dist = sqrt(dist);

  return dist;
}

/**
 * @brief Function used to determine the rotation of the fixture
*/
double set_fixture_rotation(CircleCenter * centers, int32_t center_count) 
{
  //middle_circle is the circle in the middle of the fixture
  //closer_circle is the circle that is nearest the middle circle in the fixture
  // 0----0--0 is generally what the fixture looks like
  int32_t middle_index = center_count / 2;
  CircleCenter middle_circle = centers[middle_index];
  CircleCenter closer_circle;
  int32_t rotation = 0;

  //Gets the closest circle to the center cirlce
  CircleCenter cur;
  for (int j = 1; j < center_count; j++) {
    cur = centers[(middle_index + j) % center_count];
    if (j == 1) {
      closer_circle = cur;
    } else {
      double new_distance = calculate_distance(middle_circle.x - cur.x, middle_circle.y - cur.y);
      double cur_distance = calculate_distance(middle_circle.x - closer_circle.x, middle_circle.y - closer_circle.y);
      if (new_distance < cur_distance) {
        closer_circle = cur;
      }
    }
  }

  //Set the rotation based on the these cirlce's positions
  rotation = atan((closer_circle.y - middle_circle.y) / (closer_circle.x - middle_circle.x)) * 180 / PI;
  if (closer_circle.y > middle_circle.y && closer_circle.x < middle_circle.x) {
    rotation += 180;
  } else if (closer_circle.y < middle_circle.y && closer_circle.x < middle_circle.x) {
    rotation += 180;
  } else if (closer_circle.y < middle_circle.y && closer_circle.x > middle_circle.x) {
    rotation += 360;
  }
  return rotation;
}

int main(int argc, char* argv[])
{
  const int kMaxElementCount = 6;
  double x_data[kMaxElementCount][JS_PROFILE_DATA_LEN];
  double y_data[kMaxElementCount][JS_PROFILE_DATA_LEN];
  int data_length[kMaxElementCount] = { 0 };
  int laser_on_time_us[kMaxElementCount];
  bool is_element_enabled[kMaxElementCount];
  bool is_mode_camera = false;
  GLFWwindow* window = nullptr;
  uint32_t serial_number;
  int32_t r = 0;

  //Hough transform constraints
  int32_t const circle_count = 3;
  int32_t radius = 1500;
  jsCircleHoughConstraints c;
  c.step_size = 50;
  c.x_lower = -15000;
  c.x_upper = 15000;
  c.y_lower = -30000;
  c.y_upper = 30000;
  
  //CircleCenter centers;
  CircleCenter centers[circle_count];
  jsCircleHoughResults * hough_results;
  Fixture fixture;

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
    jsCircleHough circle_hough;

    circle_hough = jsCircleHoughCreate(radius, circle_count, &c);

    app.SetSerialNumber(serial_number);
    app.Connect();
    app.SetThreshold(80);
    app.SetLaserOn(500, 100, 2000);
    app.SetWindow(40.0, -40.0, -40.0, 40.0);
    app.Configure();
    app.ConfigureDistinctElementPhaseTable();
    app.StartScanning(20000);

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
      ImGui::Text("X = %f, Y = %f   Rotation = %d", (double) fixture.x, (double) fixture.y, (uint32_t) fixture.rotation);
      //ImGui::Text("Y = %f", (double) fixture.y);
      //ImGui::Text("Rotation = %d", (int32_t) fixture.rotation);

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
      // Could also take the calculations outside of the for loop so that we only calculate center for the last profile
      // Would also have to make sure that profile exists before trying to run the calculations on it
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

        hough_results = jsCircleHoughCalculate(circle_hough, &profile);
        for(int i = 0; i < circle_count; i++) {
          centers[i].x = hough_results[i].x / 1000.0;
          centers[i].y = hough_results[i].y / 1000.0;
        }
        
        // Clear up the memory which was created in the jsCircleHough map function once we copy over the center locations
        delete hough_results;

        // Worst case, we redraw laser1 data
        for (uint32_t n = 0; n < profile.data_len; n++) {
          x_data[idx][n] = profile.data[n].x / 1000.0;
          y_data[idx][n] = profile.data[n].y / 1000.0;
        }
      }
      
      //set_fixture_position(centers, circle_count, &fixture);
      fixture.x = (centers[0].x + centers[circle_count-1].x) / 2;
      fixture.y = (centers[0].y + centers[circle_count-1].y) / 2;
      fixture.rotation = set_fixture_rotation(centers, circle_count);

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
      
      for(int k = 0; k < circle_count; k++) {
        std::string label_name = "CircleCenter " + std::to_string(k + 1);
        ImPlot::Annotation(centers[k].x, centers[k].y, ImPlot::GetLastItemColor(), ImVec2(10, 10), true, label_name.c_str());
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
