#include "ros2_camera_pub/realsense_camera.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <geometry_msgs/msg/transform_stamped.hpp>

#include "ros2_camera_pub/camera_model.hpp"

namespace ros2_camera_pub {

using nlohmann::json;

namespace {

constexpr const char * kNames[] = {"color", "depth", "ir_left", "ir_right"};

sensor_msgs::msg::CameraInfo info_from(const rs2_intrinsics & in, const std::string & frame_id) {
  sensor_msgs::msg::CameraInfo info;
  info.header.frame_id = frame_id;
  info.width = static_cast<uint32_t>(in.width);
  info.height = static_cast<uint32_t>(in.height);
  info.distortion_model = "plumb_bob";  // Brown-Conrady; D4xx colour reports near-zero coefficients
  info.d = {in.coeffs[0], in.coeffs[1], in.coeffs[2], in.coeffs[3], in.coeffs[4]};
  info.k = {in.fx, 0.0, in.ppx, 0.0, in.fy, in.ppy, 0.0, 0.0, 1.0};
  info.r = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
  info.p = {in.fx, 0.0, in.ppx, 0.0, 0.0, in.fy, in.ppy, 0.0, 0.0, 0.0, 1.0, 0.0};
  return info;
}

bool has_stream(const rs2::sensor & sensor, rs2_stream stream) {
  for (const auto & profile : sensor.get_stream_profiles()) {
    if (profile.stream_type() == stream) return true;
  }
  return false;
}

// The lens a stream profile feeds in the format it is published in, or -1.
int lens_of(const rs2::stream_profile & profile) {
  const auto type = profile.stream_type();
  const auto format = profile.format();
  if (type == RS2_STREAM_COLOR && format == RS2_FORMAT_BGR8) return 0;
  if (type == RS2_STREAM_DEPTH && format == RS2_FORMAT_Z16) return 1;
  if (type == RS2_STREAM_INFRARED && format == RS2_FORMAT_Y8) {
    if (profile.stream_index() == 1) return 2;
    if (profile.stream_index() == 2) return 3;
  }
  return -1;
}

std::string info_of(const rs2::device & device, rs2_camera_info what) {
  return device.supports(what) ? device.get_info(what) : "";
}

using Quaternion = std::array<double, 4>;  // x, y, z, w

// Of a rotation matrix, m[row][column].
Quaternion quaternion(const double m[3][3]) {
  const double trace = m[0][0] + m[1][1] + m[2][2];
  if (trace > 0.0) {
    const double s = 0.5 / std::sqrt(trace + 1.0);
    return {(m[2][1] - m[1][2]) * s, (m[0][2] - m[2][0]) * s, (m[1][0] - m[0][1]) * s, 0.25 / s};
  }
  if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
    const double s = 2.0 * std::sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
    return {0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s};
  }
  if (m[1][1] > m[2][2]) {
    const double s = 2.0 * std::sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
    return {(m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s};
  }
  const double s = 2.0 * std::sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
  return {(m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s};
}

// Roll, pitch, yaw about the fixed x, y, z axes (tf2's setRPY, static_transform_publisher's).
Quaternion quaternion_rpy(double roll, double pitch, double yaw) {
  const double cr = std::cos(roll / 2), sr = std::sin(roll / 2);
  const double cp = std::cos(pitch / 2), sp = std::sin(pitch / 2);
  const double cy = std::cos(yaw / 2), sy = std::sin(yaw / 2);
  return {sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy,
          cr * cp * cy + sr * sp * sy};
}

geometry_msgs::msg::TransformStamped transform(const std::string & parent, const std::string & child,
                                               const std::array<double, 3> & xyz, const Quaternion & q) {
  geometry_msgs::msg::TransformStamped t;
  t.header.frame_id = parent;
  t.child_frame_id = child;
  t.transform.translation.x = xyz[0];
  t.transform.translation.y = xyz[1];
  t.transform.translation.z = xyz[2];
  t.transform.rotation.x = q[0];
  t.transform.rotation.y = q[1];
  t.transform.rotation.z = q[2];
  t.transform.rotation.w = q[3];
  return t;
}

int64_t steady_ns() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
    .count();
}

}  // namespace

RealSenseCamera::RealSenseCamera(rclcpp::Node & node) : node_(node) {}

RealSenseCamera::~RealSenseCamera() {
  std::lock_guard<std::mutex> lock(control_);
  close();
}

json RealSenseCamera::devices() {
  json list = json::array();
  rs2::context context;
  for (auto && device : context.query_devices()) {
    std::array<std::set<std::tuple<int, int, int>>, kLenses> modes;
    for (auto && sensor : device.query_sensors()) {
      for (auto && profile : sensor.get_stream_profiles()) {
        const int lens = lens_of(profile);
        if (lens < 0 || !profile.is<rs2::video_stream_profile>()) continue;
        const auto video = profile.as<rs2::video_stream_profile>();
        modes[lens].insert({video.width(), video.height(), video.fps()});
      }
    }
    json lenses = json::object();
    for (int lens = 0; lens < kLenses; ++lens) {
      json entries = json::array();
      for (auto it = modes[lens].rbegin(); it != modes[lens].rend(); ++it) {  // largest first
        entries.push_back({std::get<0>(*it), std::get<1>(*it), std::get<2>(*it)});
      }
      lenses[kNames[lens]] = entries;
    }
    list.push_back({
      {"name", info_of(device, RS2_CAMERA_INFO_NAME)},
      {"serial", info_of(device, RS2_CAMERA_INFO_SERIAL_NUMBER)},
      {"usb", info_of(device, RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR)},
      {"firmware", info_of(device, RS2_CAMERA_INFO_FIRMWARE_VERSION)},
      {"modes", lenses},
    });
  }
  return list;
}

void RealSenseCamera::start(const CameraConfig & config) {
  std::lock_guard<std::mutex> lock(control_);
  close();
  if (const auto why = problem(config); !why.empty()) {
    set_problem(why);
    throw std::runtime_error(why);
  }
  try {
    open(config);
  } catch (const std::exception & error) {
    close();
    const std::string why = dynamic_cast<const rs2::error *>(&error)
                              ? std::string("RealSense: ") + error.what() : error.what();
    set_problem(why);
    throw std::runtime_error(why);
  }
}

void RealSenseCamera::stop() {
  std::lock_guard<std::mutex> lock(control_);
  close();
  set_problem("");
}

void RealSenseCamera::set_problem(const std::string & text) {
  std::lock_guard<std::mutex> lock(status_lock_);
  problem_ = text;
}

void RealSenseCamera::open(const CameraConfig & c) {
  const bool on[kLenses] = {c.color.enabled, c.depth.enabled, c.ir_left.enabled, c.ir_right.enabled};
  const bool align = c.align_depth_to_color && on[kColor] && on[kDepth];
  const auto logger = node_.get_logger();

  // The device: the one with `serial`, or the first one.
  rs2::context context;
  auto list = context.query_devices();
  if (list.size() == 0) {
    throw std::runtime_error("no RealSense found -- is it plugged into a USB 3 port, and can this process reach "
                             "/dev/bus/usb?");
  }
  std::optional<rs2::device> device;
  for (auto && candidate : list) {
    if (c.serial.empty() || c.serial == info_of(candidate, RS2_CAMERA_INFO_SERIAL_NUMBER)) {
      device = candidate;
      break;
    }
  }
  if (!device) throw std::runtime_error("no RealSense with serial " + c.serial);
  const std::string name = info_of(*device, RS2_CAMERA_INFO_NAME);
  const std::string serial = info_of(*device, RS2_CAMERA_INFO_SERIAL_NUMBER);
  const std::string usb = info_of(*device, RS2_CAMERA_INFO_USB_TYPE_DESCRIPTOR);
  RCLCPP_INFO(logger, "%s (serial %s, USB %s)", name.c_str(), serial.c_str(), usb.empty() ? "?" : usb.c_str());
  if (!usb.empty() && usb[0] == '2') {
    RCLCPP_WARN(logger, "on a USB 2 link: high resolutions and frame rates will not open -- use a USB 3 cable and "
                        "port");
  }

  rs2::config config;
  config.enable_device(serial);
  if (on[kColor]) config.enable_stream(RS2_STREAM_COLOR, c.width, c.height, RS2_FORMAT_BGR8, c.fps);
  if (on[kDepth]) config.enable_stream(RS2_STREAM_DEPTH, c.width, c.height, RS2_FORMAT_Z16, c.fps);
  if (on[kIrLeft]) config.enable_stream(RS2_STREAM_INFRARED, 1, c.width, c.height, RS2_FORMAT_Y8, c.fps);
  if (on[kIrRight]) config.enable_stream(RS2_STREAM_INFRARED, 2, c.width, c.height, RS2_FORMAT_Y8, c.fps);
  const std::string mode = std::to_string(c.width) + "x" + std::to_string(c.height) + " @ " +
                           std::to_string(c.fps) + " fps";
  try {
    profile_ = pipeline_.start(config);
    started_ = true;
  } catch (const rs2::error & error) {
    throw std::runtime_error(name + " could not open " + mode + " with these lenses (" + error.what() +
                             "): pick a mode the device lists, and keep it on USB 3");
  }

  // Exposure: colour on the sensor that carries colour (the depth module on a D405),
  // infrared on the stereo module.
  auto sensors = profile_.get_device().query_sensors();
  std::optional<rs2::sensor> color_sensor, stereo_sensor;
  for (auto && sensor : sensors) {
    if (has_stream(sensor, RS2_STREAM_COLOR) && !color_sensor) color_sensor = sensor;
    if (has_stream(sensor, RS2_STREAM_DEPTH) && !stereo_sensor) stereo_sensor = sensor;
  }
  auto set = [&logger](rs2::sensor & sensor, rs2_option option, float value, const char * what) {
    if (!sensor.supports(option)) return;
    try {
      sensor.set_option(option, value);
    } catch (const rs2::error & error) {
      RCLCPP_WARN(logger, "could not set %s to %.0f: %s", what, value, error.what());
    }
  };
  if (on[kColor] && color_sensor) {
    set(*color_sensor, RS2_OPTION_ENABLE_AUTO_EXPOSURE, c.auto_exposure ? 1.f : 0.f, "auto exposure");
    if (!c.auto_exposure) set(*color_sensor, RS2_OPTION_EXPOSURE, static_cast<float>(c.exposure), "exposure");
    if (c.gain >= 0.0) set(*color_sensor, RS2_OPTION_GAIN, static_cast<float>(c.gain), "gain");
    if (color_sensor->supports(RS2_OPTION_AUTO_EXPOSURE_PRIORITY)) {
      set(*color_sensor, RS2_OPTION_AUTO_EXPOSURE_PRIORITY, 0.f, "auto_exposure_priority");  // hold the fps
    } else if (c.auto_exposure) {
      RCLCPP_WARN(logger, "%s cannot hold the frame rate under auto exposure (no auto_exposure_priority): in dim "
                          "light it drops -- switch auto exposure off and set an exposure if it matters",
                  name.c_str());
    }
  }
  depth_scale_ = 0.001f;
  if (stereo_sensor) {
    set(*stereo_sensor, RS2_OPTION_EMITTER_ENABLED, c.emitter ? 1.f : 0.f, "emitter");
    const bool shared = color_sensor && color_sensor->get_info(RS2_CAMERA_INFO_NAME) ==
                                          stereo_sensor->get_info(RS2_CAMERA_INFO_NAME);
    const bool stereo_on = on[kDepth] || on[kIrLeft] || on[kIrRight];
    if (stereo_on && !(shared && on[kColor])) {
      set(*stereo_sensor, RS2_OPTION_ENABLE_AUTO_EXPOSURE, c.ir_auto_exposure ? 1.f : 0.f, "infrared auto exposure");
      if (!c.ir_auto_exposure) {
        set(*stereo_sensor, RS2_OPTION_EXPOSURE, static_cast<float>(c.ir_exposure), "infrared exposure");
      }
    } else if (shared && on[kColor] && stereo_on) {
      RCLCPP_INFO(logger, "%s: colour and infrared share one sensor -- the colour exposure settings apply",
                  name.c_str());
    }
    if (auto depth_sensor = stereo_sensor->as<rs2::depth_sensor>()) depth_scale_ = depth_sensor.get_depth_scale();
  }

  // Topics, camera models and the frames between the lenses.
  auto profile_of = [this](rs2_stream type, int index) {
    return profile_.get_stream(type, index).as<rs2::video_stream_profile>();
  };
  // Extrinsics hang the depth/infrared frames under the colour one; they do not depend
  // on the mode, so any colour profile serves when colour itself is off.
  std::optional<rs2::stream_profile> color_reference;
  if (on[kColor]) {
    color_reference = profile_of(RS2_STREAM_COLOR, -1);
  } else {
    for (auto && sensor : sensors) {
      for (auto && profile : sensor.get_stream_profiles()) {
        if (profile.stream_type() == RS2_STREAM_COLOR) {
          color_reference = profile;
          break;
        }
      }
      if (color_reference) break;
    }
  }
  std::vector<geometry_msgs::msg::TransformStamped> transforms;
  if (c.mount.enabled) {
    transforms.push_back(transform(c.mount.parent, c.mount.frame, c.mount.xyz,
                                   quaternion_rpy(c.mount.rpy[0], c.mount.rpy[1], c.mount.rpy[2])));
    // camera body (x forward, z up) -> optical (z forward, x right, y down)
    transforms.push_back(transform(c.mount.frame, c.frame_id, {0.0, 0.0, 0.0},
                                   quaternion_rpy(-M_PI / 2, 0.0, -M_PI / 2)));
  }
  auto under_color = [&](const rs2::stream_profile & from, const std::string & child) {
    if (!color_reference) return;
    const auto e = from.get_extrinsics_to(*color_reference);  // from -> colour, rotation column-major
    const double m[3][3] = {{e.rotation[0], e.rotation[3], e.rotation[6]},
                            {e.rotation[1], e.rotation[4], e.rotation[7]},
                            {e.rotation[2], e.rotation[5], e.rotation[8]}};
    transforms.push_back(transform(c.frame_id, child, {e.translation[0], e.translation[1], e.translation[2]},
                                   quaternion(m)));
  };
  // Reliable: Isaac ROS subscribes reliably; a reliable publisher serves best-effort ones too.
  auto make = [this](Lens lens, const StreamConfig & s) {
    Stream stream;
    stream.image = node_.create_publisher<sensor_msgs::msg::Image>(s.image_topic, 5);
    stream.info = node_.create_publisher<sensor_msgs::msg::CameraInfo>(s.info_topic, 5);
    streams_[lens] = std::move(stream);
  };
  if (on[kColor]) {
    make(kColor, c.color);
    if (c.use_custom_intrinsics) {
      streams_[kColor]->info_msg = camera_info(read_intrinsics(c.intrinsics_file), c.width, c.height, c.frame_id);
    } else {
      streams_[kColor]->info_msg = info_from(profile_of(RS2_STREAM_COLOR, -1).get_intrinsics(), c.frame_id);
    }
    RCLCPP_INFO(logger, "colour model: %s (fx %.1f fy %.1f)",
                c.use_custom_intrinsics ? c.intrinsics_file.c_str() : "factory calibration",
                streams_[kColor]->info_msg.k[0], streams_[kColor]->info_msg.k[4]);
  }
  if (on[kDepth]) {
    make(kDepth, c.depth);
    if (align) {
      streams_[kDepth]->info_msg = streams_[kColor]->info_msg;  // aligned: colour pixels, colour frame
    } else {
      streams_[kDepth]->info_msg = info_from(profile_of(RS2_STREAM_DEPTH, -1).get_intrinsics(), c.depth_frame_id);
      under_color(profile_of(RS2_STREAM_DEPTH, -1), c.depth_frame_id);
    }
  }
  if (on[kIrLeft]) {
    make(kIrLeft, c.ir_left);
    streams_[kIrLeft]->info_msg = info_from(profile_of(RS2_STREAM_INFRARED, 1).get_intrinsics(), c.depth_frame_id);
    if (!on[kDepth] || align) under_color(profile_of(RS2_STREAM_INFRARED, 1), c.depth_frame_id);  // IR 1: depth's
  }
  if (on[kIrRight]) {
    make(kIrRight, c.ir_right);
    streams_[kIrRight]->info_msg =
      info_from(profile_of(RS2_STREAM_INFRARED, 2).get_intrinsics(), c.ir_right_frame_id);
    under_color(profile_of(RS2_STREAM_INFRARED, 2), c.ir_right_frame_id);
  }
  if (!transforms.empty()) {
    tf_ = std::make_unique<tf2_ros::StaticTransformBroadcaster>(node_);
    for (auto & t : transforms) t.header.stamp = node_.now();
    tf_->sendTransform(transforms);
  }
  if (align) aligner_ = std::make_unique<rs2::align>(RS2_STREAM_COLOR);
  skip_frames_ = c.skip_frames;

  {
    std::lock_guard<std::mutex> lock(status_lock_);
    device_ = {{"name", name}, {"serial", serial}, {"usb", usb},
               {"firmware", info_of(*device, RS2_CAMERA_INFO_FIRMWARE_VERSION)}};
    mode_ = mode;
    problem_.clear();
    for (int lens = 0; lens < kLenses; ++lens) {
      topics_[lens] = streams_[lens] ? streams_[lens]->image->get_topic_name() : "";
      frames_[lens] = 0;
      counted_[lens] = 0;
    }
    counted_at_ = std::chrono::steady_clock::now();
  }
  RCLCPP_INFO(logger, "streaming %s:%s%s%s%s", mode.c_str(), on[kColor] ? " color" : "",
              on[kDepth] ? (align ? " depth(aligned)" : " depth") : "", on[kIrLeft] ? " ir_left" : "",
              on[kIrRight] ? " ir_right" : "");
  last_frame_ns_ = 0;
  running_ = true;
  worker_ = std::thread([this] { loop(); });
}

void RealSenseCamera::close() {
  running_ = false;
  if (worker_.joinable()) worker_.join();
  if (started_) {
    started_ = false;
    try {
      pipeline_.stop();
    } catch (const rs2::error &) {
    }
    RCLCPP_INFO(node_.get_logger(), "stopped");
  }
  aligner_.reset();
  for (auto & stream : streams_) stream.reset();
  tf_.reset();
  std::lock_guard<std::mutex> lock(status_lock_);
  device_ = nullptr;
  mode_.clear();
  for (auto & topic : topics_) topic.clear();
}

json RealSenseCamera::status() {
  const auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(status_lock_);
  const double seconds = std::chrono::duration<double>(now - counted_at_).count();
  json streams = json::object();
  for (int lens = 0; lens < kLenses; ++lens) {
    const uint64_t frames = frames_[lens];
    const double hz = seconds > 0.0 ? (frames - counted_[lens]) / seconds : 0.0;
    counted_[lens] = frames;
    streams[kNames[lens]] = {
      {"on", !topics_[lens].empty()},
      {"topic", topics_[lens]},
      {"hz", std::round(hz * 10.0) / 10.0},
      {"frames", frames},
    };
  }
  counted_at_ = now;
  std::string problem = problem_;
  const int64_t last = last_frame_ns_;
  if (running_ && problem.empty()) {
    const double quiet = (steady_ns() - (last ? last : steady_ns())) / 1e9;
    if (quiet > 2.0) {
      problem = "no frames for " + std::to_string(static_cast<int>(quiet)) + " s -- unplugged, or short of USB "
                "bandwidth?";
    }
  }
  return {
    {"running", running_.load()},
    {"device", device_},
    {"mode", mode_},
    {"streams", streams},
    {"problem", problem},
  };
}

void RealSenseCamera::publish(Lens lens, const rs2::video_frame & frame, const char * encoding,
                              int bytes_per_pixel, const std_msgs::msg::Header & header) {
  auto & stream = *streams_[lens];
  auto image = std::make_unique<sensor_msgs::msg::Image>();
  image->header = header;
  image->header.frame_id = stream.info_msg.header.frame_id;
  image->width = static_cast<uint32_t>(frame.get_width());
  image->height = static_cast<uint32_t>(frame.get_height());
  image->encoding = encoding;
  image->step = static_cast<uint32_t>(frame.get_width() * bytes_per_pixel);
  const auto * data = static_cast<const uint8_t *>(frame.get_data());
  const auto stride = static_cast<size_t>(frame.get_stride_in_bytes());
  if (stride == image->step) {
    image->data.assign(data, data + size_t{image->step} * image->height);
  } else {  // padded rows: copy the pixels only
    image->data.resize(size_t{image->step} * image->height);
    for (uint32_t row = 0; row < image->height; ++row) {
      std::copy_n(data + row * stride, image->step, image->data.data() + row * size_t{image->step});
    }
  }
  if (lens == kDepth && std::abs(depth_scale_ - 0.001f) > 1e-7f) {
    // Depth in millimetres whatever the device's unit (a D405 counts 0.1 mm).
    auto * values = reinterpret_cast<uint16_t *>(image->data.data());
    const float to_mm = depth_scale_ * 1000.0f;
    for (size_t i = 0; i < image->data.size() / 2; ++i) {
      values[i] = static_cast<uint16_t>(std::min(65535.0f, std::round(values[i] * to_mm)));
    }
  }
  stream.image->publish(std::move(image));
  auto info = std::make_unique<sensor_msgs::msg::CameraInfo>(stream.info_msg);
  info->header.stamp = header.stamp;
  stream.info->publish(std::move(info));
  ++frames_[lens];
}

void RealSenseCamera::loop() {
  int skipped = 0;
  while (running_ && rclcpp::ok()) {
    rs2::frameset frames;
    try {
      frames = pipeline_.wait_for_frames(1000);
    } catch (const rs2::error & error) {
      if (!running_) break;
      RCLCPP_WARN_THROTTLE(node_.get_logger(), *node_.get_clock(), 2000,
                           "no frames from the RealSense: %s -- unplugged?", error.what());
      continue;
    }
    last_frame_ns_ = steady_ns();
    if (skipped < skip_frames_) {  // exposure settles first
      ++skipped;
      continue;
    }
    if (aligner_) frames = aligner_->process(frames);
    std_msgs::msg::Header header;
    header.stamp = node_.now();
    try {
      if (streams_[kColor]) {
        if (auto color = frames.get_color_frame()) publish(kColor, color, "bgr8", 3, header);
      }
      if (streams_[kDepth]) {
        if (auto depth = frames.get_depth_frame()) publish(kDepth, depth, "16UC1", 2, header);
      }
      if (streams_[kIrLeft]) {
        if (auto ir = frames.get_infrared_frame(1)) publish(kIrLeft, ir, "mono8", 1, header);
      }
      if (streams_[kIrRight]) {
        if (auto ir = frames.get_infrared_frame(2)) publish(kIrRight, ir, "mono8", 1, header);
      }
    } catch (const std::exception & error) {  // e.g. the context shut down under us
      RCLCPP_WARN_THROTTLE(node_.get_logger(), *node_.get_clock(), 2000, "publishing failed: %s", error.what());
    }
  }
}

}  // namespace ros2_camera_pub
