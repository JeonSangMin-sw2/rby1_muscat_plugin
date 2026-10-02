// What the plugin UI sets: rby1_additional_tools' config/realsense.yaml and
// config/camera_mount.yaml in one object, kept as JSON next to the plugin.
#pragma once

#include <array>
#include <string>

#include <nlohmann/json.hpp>

namespace ros2_camera_pub {

// One lens of the RealSense and the topics it goes out on.
struct StreamConfig {
  bool enabled = false;
  std::string image_topic;
  std::string info_topic;
};

// Where the camera sits on the robot: `parent` -> `frame` (camera body: x forward,
// z up) -> the colour optical frame (z forward, x right, y down), as static TF.
struct MountConfig {
  bool enabled = true;
  std::string parent = "link_head_2";
  std::string frame = "camera_link";
  std::array<double, 3> xyz{0.022, 0.0, 0.04};  // m, in parent
  std::array<double, 3> rpy{0.0, 0.0, 0.0};     // rad, in parent
};

struct CameraConfig {
  std::string serial;  // empty: the first RealSense found
  int width = 1280;    // every stream opened at this size and rate
  int height = 720;
  int fps = 30;
  StreamConfig color{true, "/camera/image_raw", "/camera/camera_info"};                  // bgr8
  StreamConfig depth{false, "/camera/depth/image_raw", "/camera/depth/camera_info"};     // 16UC1, mm
  StreamConfig ir_left{false, "/camera/ir_left/image_raw", "/camera/ir_left/camera_info"};     // mono8
  StreamConfig ir_right{false, "/camera/ir_right/image_raw", "/camera/ir_right/camera_info"};  // mono8
  bool align_depth_to_color = false;  // depth in colour pixels and the colour frame
  std::string frame_id = "camera_optical_frame";
  std::string depth_frame_id = "camera_depth_optical_frame";  // depth and infrared 1
  std::string ir_right_frame_id = "camera_ir_right_optical_frame";
  bool auto_exposure = true;  // colour
  double exposure = 6000.0;   // us, colour, when auto_exposure is false
  double gain = -1.0;         // colour; < 0 keeps the device's
  bool ir_auto_exposure = true;
  double ir_exposure = 8500.0;  // us
  bool emitter = true;          // IR projector: on for depth, off for clean infrared
  int skip_frames = 10;         // dropped at the start while exposure settles
  bool use_custom_intrinsics = false;  // colour model from intrinsics_file instead of the factory one
  std::string intrinsics_file;
  MountConfig mount;
  bool running = false;  // streaming when last told: resumed when the plugin starts
};

// The lenses by the names the UI and the status use.
inline constexpr const char * kStreams[] = {"color", "depth", "ir_left", "ir_right"};
StreamConfig & stream(CameraConfig & config, const char * name);
const StreamConfig & stream(const CameraConfig & config, const char * name);

nlohmann::json to_json(const CameraConfig & config);
// `json` over `base`: missing keys keep base's value, unknown keys are ignored, a value
// of the wrong type throws std::invalid_argument naming the key.
CameraConfig from_json(const nlohmann::json & json, const CameraConfig & base = CameraConfig{});
// Why `config` cannot stream, or "" when it can.
std::string problem(const CameraConfig & config);

// The settings file: $ROS2_CAMERA_PUB_CONFIG, else ~/.config/<plugin_id>/camera.json.
std::string default_config_path(const std::string & plugin_id);
// Defaults when the file does not exist; throws std::runtime_error when it cannot be read.
CameraConfig load_config(const std::string & path);
void save_config(const CameraConfig & config, const std::string & path);

}  // namespace ros2_camera_pub
