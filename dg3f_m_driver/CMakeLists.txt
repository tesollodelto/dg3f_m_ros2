cmake_minimum_required(VERSION 3.8)
project(dg3f_m_driver)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(trajectory_msgs REQUIRED)
find_package(sensor_msgs REQUIRED)
find_package(delto_hardware REQUIRED)

# dg3f_m_operator_driver 빌드
add_executable(dg3f_m_operator_driver src/dg3f_m_operator_driver.cpp)
ament_target_dependencies(dg3f_m_operator_driver rclcpp trajectory_msgs sensor_msgs)

target_include_directories(dg3f_m_operator_driver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

install(TARGETS 
  dg3f_m_operator_driver
  DESTINATION lib/${PROJECT_NAME})

install(
  DIRECTORY config urdf launch
  DESTINATION share/${PROJECT_NAME}
)


### test control DG3F example
add_executable(dg3f_m_test_cpp src/dg3f_m_test.cpp)
ament_target_dependencies(dg3f_m_test_cpp rclcpp trajectory_msgs)

add_executable(dg3f_m_operator_test_cpp src/dg3f_m_operator_test.cpp)
ament_target_dependencies(dg3f_m_operator_test_cpp rclcpp trajectory_msgs)

install(
  PROGRAMS 
  script/dg3f_m_jtc_test.py
  script/dg3f_m_operator_test.py
  script/dg3f_m_effort_test.py
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  set(ament_cmake_cpplint_FOUND TRUE)
  ament_lint_auto_find_test_dependencies()
endif()

ament_package()
