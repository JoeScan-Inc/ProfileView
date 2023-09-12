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
#include <mutex>
#include <thread>

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

static std::mutex lock;
static std::vector<jsProfile> master_profiles;

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

static void receiver(jsScanHead scan_head)
{
  const int max_profiles = 100;
  jsProfile *profiles = nullptr;
  profiles = new jsProfile[max_profiles];
  auto id = jsScanHeadGetId(scan_head);

  {
    std::lock_guard<std::mutex> guard(lock);
    std::cout << "begin receiving on scan head ID " << id << std::endl;
  }

  // For this example, we'll grab some profiles and then act on the data before
  // repeating this process again. Note that for high performance applications,
  // printing to standard out while receiving data should be avoided as it
  // can add significant latency. This example only prints to standard out to
  // provide some illustrative feedback to the user, indicating that data
  // is actively being worked on in multiple threads.
  uint32_t timeout_us = 1000000;
  int32_t r = 0;
  do {
    jsScanHeadWaitUntilProfilesAvailable(scan_head, max_profiles, timeout_us);
    r = jsScanHeadGetProfiles(scan_head, profiles, max_profiles);
    if (0 < r) {
      {
        std::lock_guard<std::mutex> guard(lock);
        for (int j = 0; j < r; j++) {
          master_profiles.push_back(profiles[j]);
        }
      }
    }
  } while (0 < r);

  {
    std::lock_guard<std::mutex> guard(lock);
    std::cout << "end receiving on scan head ID " << id << std::endl;
  }

  delete[] profiles;
}

void initialize_phase_table(jsScanSystem &scan_system,
                            std::vector<jsScanHead> &scan_heads)
{
  int32_t r = 0;

  // Assume that the system is comprised of scan heads that are all the same.
  jsScanHeadType type = jsScanHeadGetType(scan_heads[0]);

  // For this example we will create a phase table that interleaves lasers
  // seen by Camera A and Camera B. This allows fast and efficient scanning
  // by allowing one camera to scan while the other has the scan data read out
  // & processed; if the same camera is used back to back, a time penalty
  // will be incurred to ensure scan data isn't overwritten.
  switch (type) {
  case (JS_SCAN_HEAD_JS50X6B20):
  case (JS_SCAN_HEAD_JS50X6B30):
    // Phase | Laser | Camera
    //   1   |   1   |   B
    //   2   |   4   |   A
    //   3   |   2   |   B
    //   4   |   5   |   A
    //   5   |   3   |   B
    //   6   |   6   |   A

    for (int n = 0; n < 3; n++) {
      jsLaser laser = JS_LASER_INVALID;

      // Lasers associated with Camera B
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw joescan::ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_1 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw joescan::ApiError("failed to insert into phase", r);
        }
      }

      // Lasers associated with Camera A
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw joescan::ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_4 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw joescan::ApiError("failed to insert into phase", r);
        }
      }
    }
    break;

  case (JS_SCAN_HEAD_JS50WSC):
  case (JS_SCAN_HEAD_JS50MX):
    // Phase | Laser | Camera
    //   1   |   1   |   A

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw joescan::ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_A);
      if (0 != r) {
        throw joescan::ApiError("failed to insert into phase", r);
      }
    }
    break;

  case (JS_SCAN_HEAD_JS50WX):
    // Phase | Laser | Camera
    //   1   |   1   |   A
    //   2   |   1   |   B

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw joescan::ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_A);
      if (0 != r) {
        throw joescan::ApiError("failed to insert into phase", r);
      }
    }

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw joescan::ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_B);
      if (0 != r) {
        throw joescan::ApiError("failed to insert into phase", r);
      }
    }
    break;

  case (JS_SCAN_HEAD_INVALID_TYPE):
  default:
    throw joescan::ApiError("invalid scan head type", 0);
  }
}

void initialize_scan_heads(jsScanSystem &scan_system,
                           std::vector<jsScanHead> &scan_heads,
                           std::vector<uint32_t> &serial_numbers)
{
  int32_t r = 0;

  jsScanHeadConfiguration config;
  config.camera_exposure_time_min_us = 10000;
  config.camera_exposure_time_def_us = 47000;
  config.camera_exposure_time_max_us = 900000;
  config.laser_on_time_min_us = 100;
  config.laser_on_time_def_us = 100;
  config.laser_on_time_max_us = 1000;
  config.laser_detection_threshold = 120;
  config.saturation_threshold = 800;
  config.saturation_percentage = 30;

  // Create a scan head for each serial number passed in on the command line
  // and configure each one with the same parameters. Note that there is
  // nothing stopping users from configuring each scan head independently.
  for (unsigned int i = 0; i < serial_numbers.size(); i++) {
    uint32_t serial = serial_numbers[i];
    auto scan_head = jsScanSystemCreateScanHead(scan_system, serial, i);
    if (0 > scan_head) {
      throw joescan::ApiError("failed to create scan head", scan_head);
    }
    scan_heads.push_back(scan_head);

    uint32_t major, minor, patch;
    r = jsScanHeadGetFirmwareVersion(scan_head, &major, &minor, &patch);
    if (0 > r) {
      throw joescan::ApiError("failed to read firmware version", r);
    }

    std::cout << serial << " v" << major << "." << minor << "." << patch
              << std::endl;

    r = jsScanHeadSetConfiguration(scan_head, &config);
    if (0 > r) {
      throw joescan::ApiError("failed to configure scan head", r);
    }

    r = jsScanHeadSetWindowRectangular(scan_head, 40.0, -40.0, -40.0, 40.0);
    if (0 > r) {
      throw joescan::ApiError("failed to set scan window", r);
    }

    r = jsScanHeadSetAlignment(scan_head, 0.0, 0.0, 0.0);
    if (0 > r) {
      throw joescan::ApiError("failed to set alignment", r);
    }
  }
}



int main(int argc, char* argv[])
{
  jsScanSystem scan_system;
  std::vector<jsScanHead> scan_heads;
  std::thread *threads = nullptr;

  const int kMaxElementCount = 6;
  const int kHeadCount = 100;
  double x_data[kHeadCount][kMaxElementCount][JS_PROFILE_DATA_LEN];
  double y_data[kHeadCount][kMaxElementCount][JS_PROFILE_DATA_LEN];
  int data_length[kHeadCount][kMaxElementCount] = { 0 };
  int laser_on_time_us[kHeadCount][kMaxElementCount];
  bool is_element_enabled[kHeadCount][kMaxElementCount];
  bool is_mode_camera = false;
  GLFWwindow* window = nullptr;
  int32_t r = 0;
  
  std::vector<uint32_t> serial_numbers;
  for (int i = 1; i < argc; i++) {
    serial_numbers.emplace_back(strtoul(argv[i], NULL, 0));
  }

  for (int j = 0; j < serial_numbers.size(); j++){
    for (int i = 0; i < kMaxElementCount; ++i) {
      is_element_enabled[j][i] = true;
    }
  }

  if (2 != argc) {
    std::cout << "Usage: " << argv[0] << " SERIAL" << std::endl;
    return 1;
  }

  std::cout << "Created variables for app" << std::endl;

  try {
    scan_system = jsScanSystemCreate(JS_UNITS_INCHES);
    if (0 > scan_system) {
      throw joescan::ApiError("jsScanSystemCreate failed", scan_system);
    }
    jsProfile profile;

    initialize_scan_heads(scan_system, scan_heads, serial_numbers);

    r = jsScanSystemConnect(scan_system, 10);
    if (0 > r) {
      throw joescan::ApiError("failed to connect to scan heads", r);
    } else if (jsScanSystemGetNumberScanHeads(scan_system) != r) {
      // On this error condition, connection was successful to some of the scan
      // heads in the system. We can query the scan heads to determine which
      // one successfully connected and which ones failed.
      for (auto scan_head : scan_heads) {
        if (false == jsScanHeadIsConnected(scan_head)) {
          uint32_t serial = jsScanHeadGetSerial(scan_head);
          std::cout << serial << " is NOT connected" << std::endl;
        }
      }
      throw joescan::ApiError("failed to connect to all scan heads", 0);
    }

    initialize_phase_table(scan_system, scan_heads);

    int32_t min_period_us = jsScanSystemGetMinScanPeriod(scan_system);
    if (0 >= min_period_us) {
      throw joescan::ApiError("failed to read min scan period", min_period_us);
    }
    std::cout << "min scan period is " << min_period_us << " us" << std::endl;

    std::cout << "start scanning" << std::endl;
    jsDataFormat data_format = JS_DATA_FORMAT_XY_BRIGHTNESS_FULL;
    r = jsScanSystemStartScanning(scan_system, min_period_us, data_format);
    if (0 > r) {
      throw joescan::ApiError("failed to start scanning", r);
    }

    threads = new std::thread[scan_heads.size()];
    for (unsigned long i = 0; i < scan_heads.size(); i++) {
      threads[i] = std::thread(receiver, scan_heads[i]);
    }
    

    jsScanHeadCapabilities cap;
    r = jsScanHeadGetCapabilities(scan_heads[0], &cap);
    if (0 > r) {
      throw joescan::ApiError("jsScanHeadGetCapabilities failed", r);
    }

    is_mode_camera = (1 == cap.num_lasers) ? true : false;
    uint32_t element_count = (is_mode_camera) ?
                             cap.num_cameras :
                             cap.num_lasers;

    std::cout << "Setting up glfw" << std::endl;

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

    std::cout << "glfw setup completed, setting up imgui" << std::endl;

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

    std::cout << "Entering main loop" << std::endl;
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

      std::cout << "Setting up checkboxes" << std::endl;

      for (int j = 0; j < serial_numbers.size(); j++) {
        char buf[64];
        for (uint32_t i = 0; i < element_count; i++) {
          if (is_mode_camera) {
            sprintf(buf, "Camera %d", i + 1);
          } else {
            sprintf(buf, "Laser %d]", i + 1);
          }

          ImGui::Checkbox(buf, &is_element_enabled[j][i]);
          ImGui::SameLine();
        }
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
      std::cout << "Setup plot" << std::endl;
      std::cout << "Getting profiles" << std::endl;
      for (int c = 0; c < kHeadCount; c++) {

        r = jsScanHeadWaitUntilProfilesAvailable(scan_heads[c], 1, 10000);
        if (0 > r) {
          throw joescan::ApiError(
            "jsScanHeadWaitUntilProfilesAvailable failed", r);
        }

        uint32_t profiles_available = r;
        for (uint32_t k = 0; k < profiles_available; k++) {
          r = jsScanHeadGetProfiles(scan_heads[c], &profile, 1);
          if (0 > r) {
            throw joescan::ApiError("jsScanHeadGetProfiles failed", r);
          }

          uint32_t idx = (is_mode_camera) ?
                        ((uint32_t) profile.camera) - 1 :
                        ((uint32_t) profile.laser) - 1;

          data_length[c][idx] = profile.data_len;
          laser_on_time_us[c][idx] = profile.laser_on_time_us;

          // Worst case, we redraw laser1 data
          for (uint32_t n = 0; n < profile.data_len; n++) {
            x_data[c][idx][n] = profile.data[n].x / 1000.0;
            y_data[c][idx][n] = profile.data[n].y / 1000.0;
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
            ImPlot::PlotScatter(legend, x_data[c][i], y_data[c][i], data_length[c][i]);
          }
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

    r = jsScanSystemStopScanning(scan_system);
    if (0 > r) {
      throw joescan::ApiError("failed to stop scanning", r);
    }

    for (unsigned long i = 0; i < scan_heads.size(); i++) {
      threads[i].join();
    }
    delete[] threads;
    threads = nullptr;

    r = jsScanSystemDisconnect(scan_system);
    if (0 > r) {
      throw joescan::ApiError("failed to disconnect from scan heads", r);
    }

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
