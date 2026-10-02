// The settings the UI sends: JSON both ways, the checks before streaming, the file.
#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

#include "ros2_camera_pub/camera_config.hpp"

using namespace ros2_camera_pub;
using nlohmann::json;

TEST(Config, DefaultsMatchTheRealSenseYaml) {
  const CameraConfig c;
  EXPECT_EQ(c.width, 1280);
  EXPECT_EQ(c.fps, 30);
  EXPECT_TRUE(c.color.enabled);
  EXPECT_FALSE(c.depth.enabled);
  EXPECT_EQ(c.color.image_topic, "/camera/image_raw");
  EXPECT_EQ(c.ir_right.info_topic, "/camera/ir_right/camera_info");
  EXPECT_EQ(c.mount.parent, "link_head_2");
  EXPECT_EQ(problem(c), "");
}

TEST(Config, JsonRoundTrips) {
  CameraConfig c;
  c.serial = "260322277376";
  c.depth.enabled = true;
  c.depth.image_topic = "/head/depth";
  c.mount.xyz = {0.1, 0.2, 0.3};
  c.exposure = 1234.5;
  const auto back = from_json(to_json(c));
  EXPECT_EQ(back.serial, c.serial);
  EXPECT_TRUE(back.depth.enabled);
  EXPECT_EQ(back.depth.image_topic, "/head/depth");
  EXPECT_DOUBLE_EQ(back.mount.xyz[2], 0.3);
  EXPECT_DOUBLE_EQ(back.exposure, 1234.5);
}

TEST(Config, PartialJsonKeepsTheRest) {
  CameraConfig base;
  base.fps = 15;
  const auto c = from_json(json{{"streams", {{"ir_left", {{"enabled", true}}}}}, {"width", 848.0}}, base);
  EXPECT_TRUE(c.ir_left.enabled);
  EXPECT_EQ(c.ir_left.image_topic, "/camera/ir_left/image_raw");
  EXPECT_EQ(c.width, 848);
  EXPECT_EQ(c.fps, 15);
}

TEST(Config, WrongTypesNameTheKey) {
  try {
    from_json(json{{"streams", {{"depth", {{"enabled", "yes"}}}}}});
    FAIL() << "no throw";
  } catch (const std::invalid_argument & error) {
    EXPECT_NE(std::string(error.what()).find("streams.depth.enabled"), std::string::npos) << error.what();
  }
  EXPECT_THROW(from_json(json::array()), std::invalid_argument);
}

TEST(Config, ProblemsStopStreaming) {
  CameraConfig c;
  c.color.enabled = false;
  EXPECT_NE(problem(c), "");  // no lens
  c.color.enabled = true;
  c.color.image_topic = "/camera/image raw";
  EXPECT_NE(problem(c), "");  // not a topic name
  c.color.image_topic = "/camera/image_raw";
  c.depth.enabled = true;
  c.depth.info_topic = "/camera/camera_info";
  EXPECT_NE(problem(c).find("twice"), std::string::npos);
  c.depth.info_topic = "/camera/depth/camera_info";
  c.use_custom_intrinsics = true;
  EXPECT_NE(problem(c), "");  // no file
  c.intrinsics_file = "/tmp/x.yaml";
  c.fps = 0;
  EXPECT_NE(problem(c), "");
  c.fps = 30;
  EXPECT_EQ(problem(c), "");
}

TEST(Config, FileKeepsTheSettings) {
  const auto path = (std::filesystem::temp_directory_path() / "ros2_camera_pub_test" / "camera.json").string();
  std::filesystem::remove(path);
  EXPECT_EQ(load_config(path).width, 1280);  // no file: defaults
  CameraConfig c;
  c.height = 480;
  c.running = true;
  save_config(c, path);
  const auto back = load_config(path);
  EXPECT_EQ(back.height, 480);
  EXPECT_TRUE(back.running);
}
