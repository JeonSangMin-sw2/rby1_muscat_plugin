#include "ros2_camera_pub/camera_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>

#include <rcl/validate_topic_name.h>

namespace ros2_camera_pub {

using nlohmann::json;

namespace {

// json[key] into `out` when it is there; a wrong type names the key.
template <typename T>
void take(const json & object, const char * key, T & out, const std::string & where = "") {
  if (!object.is_object() || !object.contains(key) || object.at(key).is_null()) return;
  try {
    out = object.at(key).get<T>();
  } catch (const json::exception &) {
    throw std::invalid_argument(where + key + ": " + std::string(object.at(key).type_name()) + " is the wrong type");
  }
}

std::string topic_problem(const std::string & topic) {
  if (topic.empty()) return "is empty";
  int result = RCL_TOPIC_NAME_VALID;
  size_t index = 0;
  if (rcl_validate_topic_name(topic.c_str(), &result, &index) != RCL_RET_OK) return "cannot be checked";
  if (result == RCL_TOPIC_NAME_VALID) return "";
  return std::string(rcl_topic_name_validation_result_string(result)) + " (at " + std::to_string(index) + ")";
}

}  // namespace

StreamConfig & stream(CameraConfig & config, const char * name) {
  const std::string key = name;
  if (key == "color") return config.color;
  if (key == "depth") return config.depth;
  if (key == "ir_left") return config.ir_left;
  if (key == "ir_right") return config.ir_right;
  throw std::invalid_argument("no stream " + key);
}

const StreamConfig & stream(const CameraConfig & config, const char * name) {
  return stream(const_cast<CameraConfig &>(config), name);
}

json to_json(const CameraConfig & c) {
  json streams = json::object();
  for (const char * name : kStreams) {
    const auto & s = stream(c, name);
    streams[name] = {{"enabled", s.enabled}, {"image_topic", s.image_topic}, {"info_topic", s.info_topic}};
  }
  return {
    {"serial", c.serial},
    {"width", c.width},
    {"height", c.height},
    {"fps", c.fps},
    {"streams", streams},
    {"align_depth_to_color", c.align_depth_to_color},
    {"frame_id", c.frame_id},
    {"depth_frame_id", c.depth_frame_id},
    {"ir_right_frame_id", c.ir_right_frame_id},
    {"auto_exposure", c.auto_exposure},
    {"exposure", c.exposure},
    {"gain", c.gain},
    {"ir_auto_exposure", c.ir_auto_exposure},
    {"ir_exposure", c.ir_exposure},
    {"emitter", c.emitter},
    {"skip_frames", c.skip_frames},
    {"use_custom_intrinsics", c.use_custom_intrinsics},
    {"intrinsics_file", c.intrinsics_file},
    {"mount",
     {{"enabled", c.mount.enabled},
      {"parent", c.mount.parent},
      {"frame", c.mount.frame},
      {"xyz", c.mount.xyz},
      {"rpy", c.mount.rpy}}},
    {"running", c.running},
  };
}

CameraConfig from_json(const json & j, const CameraConfig & base) {
  if (!j.is_object()) throw std::invalid_argument("the settings must be an object");
  CameraConfig c = base;
  take(j, "serial", c.serial);
  take(j, "width", c.width);
  take(j, "height", c.height);
  take(j, "fps", c.fps);
  if (j.contains("streams") && j.at("streams").is_object()) {
    for (const char * name : kStreams) {
      if (!j.at("streams").contains(name)) continue;
      const auto & s = j.at("streams").at(name);
      auto & out = stream(c, name);
      const std::string where = std::string("streams.") + name + ".";
      take(s, "enabled", out.enabled, where);
      take(s, "image_topic", out.image_topic, where);
      take(s, "info_topic", out.info_topic, where);
    }
  }
  take(j, "align_depth_to_color", c.align_depth_to_color);
  take(j, "frame_id", c.frame_id);
  take(j, "depth_frame_id", c.depth_frame_id);
  take(j, "ir_right_frame_id", c.ir_right_frame_id);
  take(j, "auto_exposure", c.auto_exposure);
  take(j, "exposure", c.exposure);
  take(j, "gain", c.gain);
  take(j, "ir_auto_exposure", c.ir_auto_exposure);
  take(j, "ir_exposure", c.ir_exposure);
  take(j, "emitter", c.emitter);
  take(j, "skip_frames", c.skip_frames);
  take(j, "use_custom_intrinsics", c.use_custom_intrinsics);
  take(j, "intrinsics_file", c.intrinsics_file);
  if (j.contains("mount")) {
    const auto & m = j.at("mount");
    take(m, "enabled", c.mount.enabled, "mount.");
    take(m, "parent", c.mount.parent, "mount.");
    take(m, "frame", c.mount.frame, "mount.");
    take(m, "xyz", c.mount.xyz, "mount.");
    take(m, "rpy", c.mount.rpy, "mount.");
  }
  take(j, "running", c.running);
  return c;
}

std::string problem(const CameraConfig & c) {
  if (c.width <= 0 || c.height <= 0 || c.fps <= 0) return "width, height and fps must be positive";
  bool any = false;
  std::set<std::string> topics;
  for (const char * name : kStreams) {
    const auto & s = stream(c, name);
    if (!s.enabled) continue;
    any = true;
    for (const auto * topic : {&s.image_topic, &s.info_topic}) {
      if (auto why = topic_problem(*topic); !why.empty()) {
        return std::string(name) + " topic '" + *topic + "' " + why;
      }
      if (!topics.insert(*topic).second) return "topic " + *topic + " is used twice";
    }
  }
  if (!any) return "no lens on: switch on colour, depth or an infrared stream";
  if (c.frame_id.empty() || c.depth_frame_id.empty() || c.ir_right_frame_id.empty()) {
    return "frame ids must not be empty";
  }
  if (c.skip_frames < 0) return "skip_frames must not be negative";
  if (!c.auto_exposure && !(c.exposure > 0.0)) return "exposure must be positive";
  if (!c.ir_auto_exposure && !(c.ir_exposure > 0.0)) return "infrared exposure must be positive";
  if (c.use_custom_intrinsics && c.intrinsics_file.empty()) {
    return "use_custom_intrinsics is on but intrinsics_file is empty";
  }
  if (c.mount.enabled && (c.mount.parent.empty() || c.mount.frame.empty())) {
    return "the mount needs a parent and a frame";
  }
  return "";
}

std::string default_config_path(const std::string & plugin_id) {
  if (const char * path = std::getenv("ROS2_CAMERA_PUB_CONFIG"); path && *path) return path;
  const char * home = std::getenv("HOME");
  const std::filesystem::path base = home && *home ? std::filesystem::path(home) / ".config"
                                                   : std::filesystem::temp_directory_path();
  return (base / plugin_id / "camera.json").string();
}

CameraConfig load_config(const std::string & path) {
  std::ifstream file(path);
  if (!file) return CameraConfig{};
  try {
    return from_json(json::parse(file));
  } catch (const std::exception & error) {
    throw std::runtime_error("settings file " + path + ": " + error.what());
  }
}

void save_config(const CameraConfig & config, const std::string & path) {
  const std::filesystem::path target(path);
  if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
  const auto temporary = target.string() + ".tmp";
  {
    std::ofstream file(temporary, std::ios::trunc);
    if (!file) throw std::runtime_error("cannot write " + temporary);
    file << to_json(config).dump(2) << '\n';
    if (!file) throw std::runtime_error("cannot write " + temporary);
  }
  std::filesystem::rename(temporary, target);  // whole file or the old one
}

}  // namespace ros2_camera_pub
