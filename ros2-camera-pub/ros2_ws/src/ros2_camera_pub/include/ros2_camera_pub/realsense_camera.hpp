// An Intel RealSense (librealsense, no realsense-ros) as ROS topics: only the lenses
// the settings switch on, each on its own image and camera_info topics. Ported from
// rby1_additional_tools' realsense_publisher, but started, stopped and re-set at run
// time by the plugin UI instead of once from a parameter file.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <librealsense2/rs.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <tf2_ros/static_transform_broadcaster.h>

#include "ros2_camera_pub/camera_config.hpp"

namespace ros2_camera_pub {

class RealSenseCamera {
public:
  explicit RealSenseCamera(rclcpp::Node & node);
  ~RealSenseCamera();
  RealSenseCamera(const RealSenseCamera &) = delete;
  RealSenseCamera & operator=(const RealSenseCamera &) = delete;

  // Opens the device and publishes what `config` switches on, after stopping what ran
  // before. Throws std::runtime_error saying why it could not.
  void start(const CameraConfig & config);
  void stop();
  bool running() const { return running_; }

  // running, device, per lens {topic, hz, frames}, and the last problem. Call it at a
  // steady rate: the rates are over the time since the previous call.
  nlohmann::json status();

  // The connected RealSenses and, per lens, the modes [width, height, fps] it opens.
  static nlohmann::json devices();

private:
  struct Stream {
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image;
    rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr info;
    sensor_msgs::msg::CameraInfo info_msg;
  };
  enum Lens { kColor, kDepth, kIrLeft, kIrRight, kLenses };

  void open(const CameraConfig & config);
  void close();
  void loop();
  void publish(Lens lens, const rs2::video_frame & frame, const char * encoding, int bytes_per_pixel,
               const std_msgs::msg::Header & header);
  void set_problem(const std::string & text);

  rclcpp::Node & node_;
  std::mutex control_;  // start/stop
  rs2::pipeline pipeline_;
  rs2::pipeline_profile profile_;
  bool started_ = false;  // pipeline_ streaming
  std::unique_ptr<rs2::align> aligner_;
  float depth_scale_ = 0.001f;
  int skip_frames_ = 0;
  std::array<std::optional<Stream>, kLenses> streams_;
  std::unique_ptr<tf2_ros::StaticTransformBroadcaster> tf_;
  std::atomic<bool> running_{false};
  std::thread worker_;

  // Status, read by status() from another thread.
  std::array<std::atomic<uint64_t>, kLenses> frames_{};
  std::atomic<int64_t> last_frame_ns_{0};
  std::mutex status_lock_;
  nlohmann::json device_ = nullptr;
  std::array<std::string, kLenses> topics_;
  std::string problem_;
  std::string mode_;
  std::array<uint64_t, kLenses> counted_{};
  std::chrono::steady_clock::time_point counted_at_ = std::chrono::steady_clock::now();
};

}  // namespace ros2_camera_pub
