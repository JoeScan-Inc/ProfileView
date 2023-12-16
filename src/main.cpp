/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#include "imgui.h"
#include "implot.h"
#include "joescan_pinchot.h"
#include <atomic>
#include <vector>
#include <iostream>
#include <fstream>
#include <thread>
#include <queue>
#include <mutex>

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl2.h"
#include <stdio.h>
#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include <string>
#include <sstream>

static std::atomic<bool> _is_scanning(false);
static std::atomic<uint32_t> _frame_count(0);
static std::atomic<uint32_t> _profile_count(0);
static std::atomic<uint32_t> _invalid_count(0);
static std::queue<jsProfile*> master_profiles;
static std::mutex master_profiles_mutex;

#if defined(_WIN32)
#include "Windows.h"
#include "processthreadsapi.h"

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to
// maximize ease of testing and compatibility with old VS compilers. To link
// with VS2010-era libraries, VS2015+ requires linking with 
// legacy_stdio_definitions.lib, which we do using this pragma. Your own
// project should not be affected, as you are likely to link with a newer
// binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && \
    (_MSC_VER >= 1900) && \
    !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

BOOL WINAPI signal_callback_handler(DWORD signal) {

  if ((CTRL_C_EVENT == signal) || (CTRL_BREAK_EVENT == signal)) {
    std::cout << "Received signal " << signal << std::endl;
    _is_scanning = false;
    return TRUE;
  }

  LOG_INFO << "Received unhandled signal " << signal;

  return FALSE;
}
#else
#include <csignal>

void signal_callback_handler(int signum)
{
  std::cout << "Received signal " << signum << std::endl;
  _is_scanning = false;
}
#endif

class ApiError : public std::runtime_error {
 private:
  jsError m_return_code;

 public:
  ApiError(const char* what, int32_t return_code) : std::runtime_error(what)
  {
    if ((0 < return_code) || (JS_ERROR_UNKNOWN > m_return_code)) {
      m_return_code = JS_ERROR_UNKNOWN;
    } else {
      m_return_code = (jsError) return_code;
    }
  }

  jsError return_code() const
  {
    return m_return_code;
  }
};

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
      throw ApiError("failed to create scan head", scan_head);
    }
    scan_heads.push_back(scan_head);

    uint32_t major, minor, patch;
    r = jsScanHeadGetFirmwareVersion(scan_head, &major, &minor, &patch);
    if (0 > r) {
      throw ApiError("failed to read firmware version", r);
    }

    std::cout << serial << " v" << major << "." << minor << "." << patch
              << std::endl;

    r = jsScanHeadSetConfiguration(scan_head, &config);
    if (0 > r) {
      throw ApiError("failed to set scan head configuration", r);
    }

    std::cout << serial << ": scan window is 30, -30, -30, 30" << std::endl;
    r = jsScanHeadSetWindowRectangular(scan_head, 30.0, -30.0, -30.0, 30.0);
    if (0 > r) {
      throw ApiError("failed to set window", r);
    }

    r = jsScanHeadSetAlignment(scan_head, 0.0, 0.0, 0.0);
    if (0 > r) {
      throw ApiError("failed to set alignment", r);
    }
  }
}

/**
 * @brief Creates a basic phase table using all the scan heads managed by the
 * scan system.
 *
 * @param scan_system Reference to the scan system.
 * @param scan_heads Reference to vector of all created scan heads.
 */
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
        throw ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_1 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
        }
      }

      // Lasers associated with Camera A
      r = jsScanSystemPhaseCreate(scan_system);
      if (0 != r) {
        throw ApiError("failed to create phase", r);
      }

      laser = (jsLaser) (JS_LASER_4 + n);
      for (auto scan_head : scan_heads) {
        r = jsScanSystemPhaseInsertLaser(scan_system, scan_head, laser);
        if (0 != r) {
          throw ApiError("failed to insert into phase", r);
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
      throw ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_A);
      if (0 != r) {
        throw ApiError("failed to insert into phase", r);
      }
    }
    break;

  case (JS_SCAN_HEAD_JS50WX):
    // Phase | Laser | Camera
    //   1   |   1   |   A
    //   2   |   1   |   B

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_A);
      if (0 != r) {
        throw ApiError("failed to insert into phase", r);
      }
    }

    r = jsScanSystemPhaseCreate(scan_system);
    if (0 != r) {
      throw ApiError("failed to create phase", r);
    }

    for (auto scan_head : scan_heads) {
      r = jsScanSystemPhaseInsertCamera(scan_system, scan_head, JS_CAMERA_B);
      if (0 != r) {
        throw ApiError("failed to insert into phase", r);
      }
    }
    break;

  case (JS_SCAN_HEAD_INVALID_TYPE):
  default:
    throw ApiError("invalid scan head type", 0);
  }
}

int main(int argc, char* argv[])
{
  const int kMaxElementCount = 8;
  double x_data[kMaxElementCount][JS_PROFILE_DATA_LEN];
  double y_data[kMaxElementCount][JS_PROFILE_DATA_LEN];
  int data_length[kMaxElementCount] = { 0 };
  int laser_on_time_us[kMaxElementCount];
  bool is_mode_camera = false;
  GLFWwindow* window = nullptr;
  jsProfile *profiles = nullptr;
  // std::vector<bool> is a mess in C++...
  bool *is_element_enabled = nullptr;
  float ***colors = nullptr;
  int32_t r = 0;

  jsScanSystem scan_system;
  std::vector<jsScanHead> scan_heads;
  std::vector<uint32_t> serial_numbers;
  std::thread thread;

  if (2 > argc) {
    std::cout << "Usage: " << argv[0] << " SERIAL..." << std::endl;
    return 1;
  }

#if defined(_WIN32)
  if (!SetConsoleCtrlHandler(console_handler, TRUE)) {
   std::cout << "Could not set control handler" << std::endl;
    return 1;
  }
  LOG_INFO << "Registered console ctrl handler";
#else
  signal(SIGTERM, signal_callback_handler);
  std::cout << "Registered SIGTERM signal handler" << std::endl;
  signal(SIGINT, signal_callback_handler);
  std::cout << "Registered SIGINT signal handler" << std::endl;
#endif

  // Grab the serial number(s) passed in through the command line.
  for (int i = 1; i < argc; i++) {
    serial_numbers.emplace_back(strtoul(argv[i], NULL, 0));
  }
  is_element_enabled = new bool[serial_numbers.size()];
  std::fill_n(is_element_enabled, serial_numbers.size(), true);

  colors = new float**[serial_numbers.size()];
  for (int m = 0; m < serial_numbers.size(); m++) {
    colors[m] = new float*[kMaxElementCount];
    for (int n = 0; n < kMaxElementCount; n++) {
      colors[m][n] = new float[4];
      if (n % 2 == 0) {
        colors[m][n][0] = 0.9882;
        colors[m][n][1] = 0.5647;
        colors[m][n][2] = 0.0117;
        colors[m][n][3] = 1.0f;
      } else {
        colors[m][n][0] = 0.0117;
        colors[m][n][1] = 0.5647;
        colors[m][n][2] = 0.9882;
        colors[m][n][3] = 1.0f;
      }
    }
  }

  try {
    // First step is to create a scan manager to manage the scan heads.
    scan_system = jsScanSystemCreate(JS_UNITS_INCHES);
    if (0 > scan_system) {
      throw ApiError("failed to create scan system", scan_system);
    }

    initialize_scan_heads(scan_system, scan_heads, serial_numbers);

    r = jsScanSystemConnect(scan_system, 10);
    if (0 > r) {
      throw ApiError("failed to connect", r);
    } else if (jsScanSystemGetNumberScanHeads(scan_system) != r) {
      for (auto scan_head : scan_heads) {
        if (false == jsScanHeadIsConnected(scan_head)) {
          uint32_t serial = jsScanHeadGetSerial(scan_head);
          std::cout << serial << " is NOT connected" << std::endl;
        }
      }
      throw ApiError("fgenitive englishailed to connect to all scan heads", 0);
    }

    initialize_phase_table(scan_system, scan_heads);

    jsScanHeadCapabilities cap;
    r = jsScanHeadGetCapabilities(scan_heads[0], &cap);
    if (0 > r) {
      throw ApiError("jsScanHeadGetCapabilities failed", r);
    }

    is_mode_camera = (1 == cap.num_lasers) ? true : false;
    uint32_t element_count = (is_mode_camera) ?
                             cap.num_cameras :
                             cap.num_lasers;

    r = jsScanSystemGetProfilesPerFrame(scan_system);
    if (0 >= r) {
      throw ApiError("failed to read frame size", r);
    }
    uint32_t profiles_per_frame = uint32_t(r);
    profiles = new jsProfile[profiles_per_frame];

    int32_t min_period_us = jsScanSystemGetMinScanPeriod(scan_system);
    if (0 >= min_period_us) {
      throw ApiError("failed to read min scan period", min_period_us);
    }
    std::cout << "min scan period is " << min_period_us << " us" << std::endl;
    const uint32_t kMinScanPeriodUs = 100000;
    if (min_period_us < kMinScanPeriodUs) {
      min_period_us = kMinScanPeriodUs;
    }

    std::cout << "starting scan with " << min_period_us << " us scan period" 
              << std::endl;
    jsDataFormat data_format = JS_DATA_FORMAT_XY_BRIGHTNESS_FULL;
    r = jsScanSystemStartFrameScanning(scan_system, min_period_us, data_format);
    if (0 > r) {
      throw ApiError("failed to start scanning", r);
    }
    _is_scanning = true;

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
      throw std::runtime_error("glfwSetErrorCallback() failed");
    }

    auto monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);
    glfwWindowHint(GLFW_RED_BITS, mode->redBits);
    glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
    glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    window = glfwCreateWindow(
      1920,
      1200,
      "ProfileView",
      NULL,
      NULL
    );

    if (nullptr == window) {
      throw std::runtime_error("glfwCreateWindow() failed");
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
    int last_failed_sn = 0;
    int last_sn = 0;
    int valid_frames = 0;
    // Main loop
    while ((!glfwWindowShouldClose(window)) && _is_scanning) {
      int64_t encoder;
      uint32_t head;

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
      ImGui::Begin(
        "ScanData",
        &open,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize
      );
      
      ImGui::Columns(2);
      ImGui::SetColumnWidth(0, 200);
      for (int m = 0; m < serial_numbers.size(); m++) {
        char buf[64];
        sprintf(buf, "Scan Head %d", serial_numbers[m]);
        ImGui::Checkbox(buf, &is_element_enabled[m]);
        ImGui::Indent();
        for (int n = 0; n < element_count; n++) {
          char element_buf[64];
          char invisible_buf[64];
          sprintf(element_buf, "Element %d", ((m*2) + n+1));
          sprintf(invisible_buf, "##%d", ((m*2) + n+1));
          ImGui::ColorEdit4(
            element_buf,
            colors[m][n],
            ImGuiColorEditFlags_NoInputs
          );
        }
        ImGui::Unindent();
      }

      ImGui::Text("Encoder = %lu", encoder);
      ImGui::NextColumn();

      auto is_plot_sucess = ImPlot::BeginPlot(
        "Profile Plot",
        "X [inches]",
        "Y [inches]",
        ImVec2(-1, -1),
        ImPlotFlags_Equal
      );
      if (!is_plot_sucess) {
        continue;
      }

      ImPlot::SetupAxesLimits(-50.0, 50.0, -50.0, 50.0);
      ImPlot::SetupFinish();


      r = jsScanSystemWaitUntilFrameAvailable(scan_system, 1000000);
      if (0 == r) {
        std::cout << "wait failed!" << std::endl;
      } else if (0 > r) {
        throw ApiError("failed to wait for frame", r);
      }

      for (uint32_t n = 0; n < profiles_per_frame; n++) {
        jsProfileInit(&profiles[n]);
      }

      r = jsScanSystemGetFrame(scan_system, profiles);
      if (0 == r) {
        std::cout << "no frame? " << std::to_string(_frame_count) << std::endl;
      } else if (0 > r) {
        throw ApiError("failed to read frame", r);
      } 
      _frame_count++;
      for (uint32_t n = 0; n < profiles_per_frame; n++) {
        if (!jsProfileIsValid(profiles[n])) {
          continue;
        }

        jsProfile *p = &profiles[n];
        head = p->scan_head_id;
        encoder = p->encoder_values[0];
        uint32_t idx = (is_mode_camera) ?
                       ((uint32_t) p->camera) - 1 :
                       ((uint32_t) p->laser) - 1;

        for (uint32_t i = 0; i < p->data_len; i++) {
          x_data[idx][i] = p->data[i].x * 0.001;
          y_data[idx][i] = p->data[i].y * 0.001;
        }
        data_length[idx] = p->data_len;
        laser_on_time_us[idx] = p->laser_on_time_us;

        ImPlot::SetNextMarkerStyle(
          ImPlotMarker_Square,
          1,
          ImVec4(
            colors[head][idx][0],
            colors[head][idx][1],
            colors[head][idx][2],
            colors[head][idx][3]),
          IMPLOT_AUTO,
          ImVec4(
            colors[head][idx][0],
            colors[head][idx][1],
            colors[head][idx][2],
            colors[head][idx][3])
        );

        char legend[32];
        if (is_mode_camera) {
          sprintf(legend, "Camera %d##%d", (2*head) + (idx+1), 1);
        } else {
          sprintf(legend, "Laser %d##%d", (2*head) + (idx+1), 1);
        }

        if (is_element_enabled[head]) {
          ImPlot::PlotScatter(
            legend,
            x_data[idx],
            y_data[idx],
            data_length[idx]
          );
        }
      }

      ImPlot::EndPlot();
      ImGui::End();
      ImGui::PopStyleVar();
      ImGui::Render();
      int display_w, display_h;
      glfwGetFramebufferSize(window, &display_w, &display_h);
      glViewport(0, 0, display_w, display_h);
      glClearColor(
        (clear_color.x * clear_color.w),
        (clear_color.y * clear_color.w),
        (clear_color.z * clear_color.w),
        clear_color.w
      );
      glClear(GL_COLOR_BUFFER_BIT);
      ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
      glfwMakeContextCurrent(window);
      glfwSwapBuffers(window);
    }
  } catch (ApiError &e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    r = 1;

    const char *err_str = nullptr;
    jsError err = e.return_code();
    if (JS_ERROR_NONE != err) {
      jsGetError(err, &err_str);
      std::cout << "jsError (" << err << "): " << err_str << std::endl;
    }
  } catch (std::runtime_error &e) {
    std::cout << "ERROR: " << e.what() << std::endl;
    r = 1;
  }

  std::cout << "Exiting..." << std::endl;

  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  jsScanSystemStopScanning(scan_system);
  jsScanSystemDisconnect(scan_system);
  jsScanSystemFree(scan_system);

  if (nullptr != colors) {
    for (int i = 0; i < serial_numbers.size(); i++) {
      for (int j = 0; j < kMaxElementCount; j++) {
        delete[] colors[i][j];
      }
      delete[] colors[i];
    }
    delete[] colors;
  }

  if (nullptr != profiles) {
    delete profiles;
  }

  if (nullptr != is_element_enabled) {
    delete is_element_enabled;
  }

  return 0;
}
