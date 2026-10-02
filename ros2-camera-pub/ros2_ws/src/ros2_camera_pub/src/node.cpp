// Ros2 Camera Pub -- an Intel RealSense as ROS topics, set up from the plugin UI.
//
// The plugin UI talks to this node through RBPluginCall:
//   rb.action("devices")                 connected RealSenses and the modes of each lens
//   rb.action("config/get")              the settings, the defaults and the status
//   rb.action("config/apply", {config})  check and keep the settings; restart if streaming
//   rb.action("start", {config})         apply, then stream
//   rb.action("stop")                    stop streaming
//   rb.sub("status")                     once a second: device, per-lens topic and rate, problem
// Every reply carries {ok, error?}.
//
// The settings are a JSON file (ROS parameter `config_file`, default
// ~/.config/<plugin_id>/camera.json) that also keeps whether the camera was streaming,
// so it streams again when the plugin restarts.
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>
#include <rainbow/rb_sdk/plugin/rb_plugin.hpp>
#include <rclcpp/rclcpp.hpp>

#include "ros2_camera_pub/camera_config.hpp"
#include "ros2_camera_pub/realsense_camera.hpp"

using namespace std::chrono_literals;
using nlohmann::json;

namespace ros2_camera_pub {

namespace {

// Muscat sets RB_PLUGIN_ID; run by hand, the manifest's id.
std::string plugin_id() {
  try {
    return rb_plugin::RBPluginCall::resolve_plugin_id(std::nullopt);
  } catch (const std::exception &) {
    return "com.example.ros2-camera-pub";
  }
}

json failure(const std::string & why) { return {{"ok", false}, {"error", why}}; }

}  // namespace

class CameraPubNode : public rclcpp::Node {
public:
  CameraPubNode() : rclcpp::Node("ros2_camera_pub_node"), camera_(*this) {
    const auto id = plugin_id();
    config_path_ = declare_parameter("config_file", default_config_path(id));
    try {
      config_ = load_config(config_path_);
    } catch (const std::exception & error) {
      RCLCPP_ERROR(get_logger(), "%s -- starting from the defaults", error.what());
    }
    RCLCPP_INFO(get_logger(), "plugin %s, settings in %s", id.c_str(), config_path_.c_str());

    plugin_ = rb_plugin::RBPluginCall::get_instance(id);
    serve("devices", [](const json &) { return json{{"ok", true}, {"devices", RealSenseCamera::devices()}}; });
    serve("config/get", [this](const json &) {
      std::lock_guard<std::mutex> lock(lock_);
      return json{{"ok", true}, {"config", to_json(config_)}, {"defaults", to_json(CameraConfig{})},
                  {"status", status()}};
    });
    serve("config/apply", [this](const json & payload) { return apply(payload, std::nullopt); });
    serve("start", [this](const json & payload) { return apply(payload, true); });
    serve("stop", [this](const json & payload) { return apply(payload, false); });
    serve("status", [this](const json &) { return json{{"ok", true}, {"status", status()}}; });

    if (config_.running) {
      try {
        camera_.start(config_);
      } catch (const std::exception & error) {
        RCLCPP_ERROR(get_logger(), "could not resume streaming: %s", error.what());
      }
    }
    publish_status();
    timer_ = create_wall_timer(1s, [this] { publish_status(); });
  }

  ~CameraPubNode() override {
    timer_.reset();
    plugin_->unserve();  // the handlers point at this node
  }

private:
  // A handler whose exceptions come back to the UI as {ok: false, error}.
  void serve(const std::string & topic, std::function<json(const json &)> handler) {
    plugin_->serve(topic, [this, topic, handler](const json & payload) {
      try {
        return handler(payload);
      } catch (const std::exception & error) {
        RCLCPP_WARN(get_logger(), "%s: %s", topic.c_str(), error.what());
        return failure(error.what());
      }
    });
  }

  // Takes payload.config over the current settings, then streams with them (run true),
  // stops (run false) or restarts if streaming (run unset), and keeps them.
  json apply(const json & payload, std::optional<bool> run) {
    std::lock_guard<std::mutex> lock(lock_);
    CameraConfig next = config_;
    if (payload.is_object() && payload.contains("config")) next = from_json(payload.at("config"), config_);
    const bool stream = run.value_or(camera_.running());
    if (run != false) {
      if (const auto why = problem(next); !why.empty()) return failure(why);
    }
    std::string error;
    if (stream) {
      try {
        camera_.start(next);
      } catch (const std::exception & e) {
        error = e.what();
      }
    } else {
      camera_.stop();
    }
    next.running = camera_.running();
    config_ = next;
    try {
      save_config(config_, config_path_);
    } catch (const std::exception & e) {
      error += (error.empty() ? "" : "; ") + std::string("settings not saved: ") + e.what();
    }
    publish_status();
    json reply = {{"ok", error.empty()}, {"config", to_json(config_)}, {"status", status()}};
    if (!error.empty()) reply["error"] = error;
    return reply;
  }

  void publish_status() {
    auto now = camera_.status();
    now["settings_file"] = config_path_;
    {
      std::lock_guard<std::mutex> lock(status_lock_);
      status_ = now;
    }
    plugin_->publish("status", now);  // dropped while the UI gateway is not there
  }

  json status() {
    std::lock_guard<std::mutex> lock(status_lock_);
    return status_;
  }

  RealSenseCamera camera_;
  std::shared_ptr<rb_plugin::RBPluginCall> plugin_;
  std::string config_path_;
  std::mutex lock_;  // config_ and the camera's start/stop, between UI calls
  CameraConfig config_;
  std::mutex status_lock_;
  json status_ = json::object();
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace ros2_camera_pub

int main(int argc, char ** argv) {
  rclcpp::init(argc, argv);
  int code = 0;
  try {
    rclcpp::spin(std::make_shared<ros2_camera_pub::CameraPubNode>());
  } catch (const std::exception & error) {
    RCLCPP_FATAL(rclcpp::get_logger("ros2_camera_pub_node"), "%s", error.what());
    code = 1;
  }
  rclcpp::shutdown();
  return code;
}
