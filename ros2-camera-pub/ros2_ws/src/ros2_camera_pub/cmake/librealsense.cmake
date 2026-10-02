# librealsense2, built from source and linked statically: the robot has only Ubuntu
# 24.04 base libraries (+ ROS), so the plugin carries its own camera driver. RSUSB
# backend (libusb, no kernel patches or V4L2 metadata modules), no DDS (Intel's DDS
# build clashes with ROS's Fast DDS in one process), no viewer/tools/examples.
#
# libusb is built here too, without udev (libudev-dev is not in the build image;
# libusb then enumerates over sysfs/netlink), and handed to librealsense as found
# so it does not build its own udev-linked copy.
#
# Result: the IMPORTED target realsense2::static. Downloads at the first build
# (GitHub); later builds reuse the build directory.
include(ExternalProject)

set(RS2_VERSION "v2.58.4" CACHE STRING "librealsense tag to build")
set(RS2_PREFIX ${CMAKE_BINARY_DIR}/rs2)
set(RS2_LIB ${RS2_PREFIX}/lib/librealsense2-all.a)
set(USB_LIB ${RS2_PREFIX}/lib/libusb-1.0.a)
include(ProcessorCount)
ProcessorCount(RS2_JOBS)
if(RS2_JOBS EQUAL 0)
  set(RS2_JOBS 4)
endif()

ExternalProject_Add(libusb_ext
  GIT_REPOSITORY https://github.com/libusb/libusb-cmake.git
  GIT_TAG v1.0.27-1
  GIT_SHALLOW TRUE
  UPDATE_COMMAND ""
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${RS2_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DLIBUSB_ENABLE_UDEV=OFF
    -DLIBUSB_BUILD_EXAMPLES=OFF
    -DLIBUSB_BUILD_TESTING=OFF
    -DLIBUSB_INSTALL_TARGETS=ON
  BUILD_BYPRODUCTS ${USB_LIB})

ExternalProject_Add(librealsense_ext
  DEPENDS libusb_ext
  GIT_REPOSITORY https://github.com/IntelRealSense/librealsense.git
  GIT_TAG ${RS2_VERSION}
  GIT_SHALLOW TRUE
  UPDATE_COMMAND ""
  CMAKE_ARGS
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_INSTALL_PREFIX=${RS2_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_RS2_ALL=ON
    -DFORCE_RSUSB_BACKEND=ON
    -DLIBUSB_LIB=${USB_LIB}
    -DLIBUSB_INC=${RS2_PREFIX}/include/libusb-1.0
    -DBUILD_WITH_DDS=OFF
    -DBUILD_ROSBAG2=OFF
    -DBUILD_EXAMPLES=OFF
    -DBUILD_GRAPHICAL_EXAMPLES=OFF
    -DBUILD_GLSL_EXTENSIONS=OFF
    -DBUILD_TOOLS=OFF
    -DBUILD_UNIT_TESTS=OFF
    -DBUILD_PYTHON_BINDINGS=OFF
    -DCHECK_FOR_UPDATES=OFF
    -DENABLE_CCACHE=OFF
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config Release --parallel ${RS2_JOBS}
  # The all-in-one archive stays in the build tree; install only the headers next to it.
  INSTALL_COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include/librealsense2 ${RS2_PREFIX}/include/librealsense2
    COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/Release/librealsense2-all.a ${RS2_LIB}
  BUILD_BYPRODUCTS ${RS2_LIB})

# The include directory must exist at configure time for the imported target.
file(MAKE_DIRECTORY ${RS2_PREFIX}/include)
find_package(Threads REQUIRED)
add_library(realsense2::static STATIC IMPORTED GLOBAL)
set_target_properties(realsense2::static PROPERTIES
  IMPORTED_LOCATION ${RS2_LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${RS2_PREFIX}/include
  INTERFACE_LINK_LIBRARIES "${USB_LIB};Threads::Threads;${CMAKE_DL_LIBS}")
add_dependencies(realsense2::static librealsense_ext)
