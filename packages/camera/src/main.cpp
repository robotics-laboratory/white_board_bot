#include "camera/camera_node.h"

#include <rclcpp/rclcpp.hpp>
#include <memory>

int main(int argc, char* argv[]) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_unique<wbb::CameraNode>());
    rclcpp::shutdown();
    return 0;
}
