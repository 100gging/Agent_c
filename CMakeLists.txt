cmake_minimum_required(VERSION 3.16)
project(cv7_imu_test LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Reuse the in-repo MIP SDK used by tofu_archive.
add_subdirectory(
    ../tofu_archive-master/third_party/devices/imu/mip_sdk/mip_sdk
    ${CMAKE_BINARY_DIR}/mip_sdk_build
    EXCLUDE_FROM_ALL
)

add_executable(imu_cv7_test imu_cv7_test.cpp)
target_link_libraries(imu_cv7_test PRIVATE mip microstrain_serial)
target_compile_options(imu_cv7_test PRIVATE -fpermissive)

# --- Two-link URDF + IMU test ---
find_package(tinyxml2 REQUIRED)

add_executable(imu_two_link_test imu_two_link_test.cpp)
target_link_libraries(imu_two_link_test PRIVATE mip microstrain_serial tinyxml2::tinyxml2)
target_compile_options(imu_two_link_test PRIVATE -fpermissive)
