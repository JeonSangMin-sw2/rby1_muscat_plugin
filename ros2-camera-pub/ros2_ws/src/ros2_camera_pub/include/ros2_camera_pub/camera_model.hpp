// Camera models from a calibration file (rby1_additional_tools' camera_model, the
// part the RealSense needs).
#pragma once

#include <array>
#include <string>
#include <vector>

#include <sensor_msgs/msg/camera_info.hpp>

namespace ros2_camera_pub {

// A calibrated camera: 3x3 camera matrix (row-major) and plumb_bob distortion
// (k1, k2, p1, p2, k3) at the image size it was measured for.
struct Intrinsics {
  int width = 0, height = 0;
  std::array<double, 9> k{};
  std::vector<double> d;
  std::string model = "plumb_bob";
};

// An intrinsics YAML file, in either layout:
//   camera_ws (OpenCV):  width, height, camera_matrix: [[fx,0,cx],[0,fy,cy],[0,0,1]], dist_coeffs: [...]
//   ROS calibration:     image_width, image_height, camera_matrix: {data: [9]},
//                        distortion_model, distortion_coefficients: {data: [...]}
Intrinsics read_intrinsics(const std::string & path);

// The CameraInfo for an image of `width` x `height`: the camera matrix scaled if the
// file was measured at another size with the same aspect ratio (refused otherwise).
sensor_msgs::msg::CameraInfo camera_info(const Intrinsics & intrinsics, int width, int height,
                                         const std::string & frame_id);

}  // namespace ros2_camera_pub
