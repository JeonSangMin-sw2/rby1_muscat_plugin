cmake_minimum_required(VERSION 3.16)
project(ros2_camera_pub)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra)
endif()

find_package(ament_cmake REQUIRED)
find_package(rcl REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(tf2_ros REQUIRED)
find_package(yaml-cpp REQUIRED)
# Linked in when the static archive is there (libyaml-cpp-dev ships it): one runtime
# library fewer to expect on the robot.
find_library(YAML_CPP_STATIC NAMES libyaml-cpp.a)
if(YAML_CPP_STATIC)
  set(YAML_CPP_LINK ${YAML_CPP_STATIC})
else()
  set(YAML_CPP_LINK yaml-cpp)
endif()
# Rainbow Robotics C++ SDK (apt: rainbow-rb-sdk-dev, pre-installed in the dev
# container): RBPluginCall talks to the plugin UI.
find_package(rb_sdk REQUIRED)
# librealsense2, static (see cmake/librealsense.cmake).
include(cmake/librealsense.cmake)

add_library(${PROJECT_NAME}_core STATIC src/camera_model.cpp src/camera_config.cpp)
target_include_directories(${PROJECT_NAME}_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
ament_target_dependencies(${PROJECT_NAME}_core rcl sensor_msgs)
target_include_directories(${PROJECT_NAME}_core PRIVATE ${YAML_CPP_INCLUDE_DIR})
target_link_libraries(${PROJECT_NAME}_core ${YAML_CPP_LINK} rb_sdk::rb_sdk)

add_executable(ros2_camera_pub_node src/node.cpp src/realsense_camera.cpp)
target_link_libraries(ros2_camera_pub_node ${PROJECT_NAME}_core realsense2::static rb_sdk::rb_sdk)
ament_target_dependencies(ros2_camera_pub_node rclcpp sensor_msgs geometry_msgs tf2_ros)

install(TARGETS ros2_camera_pub_node
  DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest QUIET)
  if(ament_cmake_gtest_FOUND)
    ament_add_gtest(test_camera_model test/test_camera_model.cpp test/test_camera_config.cpp)
    target_link_libraries(test_camera_model ${PROJECT_NAME}_core)
  endif()
endif()

ament_package()
