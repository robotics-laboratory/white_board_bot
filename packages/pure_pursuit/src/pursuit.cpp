#include "pure_pursuit/pursuit.h"

namespace wbb
{

PurePursuit::PurePursuit() : Node("pure_pursuit")
{
    const auto qos = static_cast<rmw_qos_reliability_policy_t>(
        this->declare_parameter<int>("qos", RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT));

    std::chrono::duration<double> period = std::chrono::duration<double>(
        this->declare_parameter<double>("period", 0.02));
    lookahead_distance = this->declare_parameter<double>("lookahead", 0.2);
    trajectory_pos = 0;

    timer_ = this->create_wall_timer(period, std::bind(&PurePursuit::sendControlCommand, this));

    slot_.trajectory = this->create_subscription<wbb_msgs::msg::ImagePath>(
        "/robot/motion", 1, std::bind(&PurePursuit::handleTrajectory, this, _1)
        );

    slot_.bot_pose = this->create_subscription<wbb_msgs::msg::ImagePose>(
        "/robot/ego", rclcpp::QoS(1).reliability(qos),
        std::bind(&PurePursuit::handleBotPose, this, _1)
        );

    slot_.scale = this->create_subscription<wbb_msgs::msg::ImagePixelScale>(
        "/board/scale", 1, std::bind(&PurePursuit::handlePixelScale, this, _1)
        );

    signal_.control = this->create_publisher<wbb_msgs::msg::Control>("/movement", 1);
    signal_.visual = this->create_publisher<visualization_msgs::msg::ImageMarker>("/robot/control/marker", 10);
}

void PurePursuit::handleTrajectory(wbb_msgs::msg::ImagePath::SharedPtr trajectory)
{
    if (trajectory != state_.trajectory)
    {
        state_.trajectory = std::move(trajectory);
        trajectory_pos = 0;
    }
}

void PurePursuit::handleBotPose(wbb_msgs::msg::ImagePose::SharedPtr bot_pose)
{
    state_.bot_pose = std::move(bot_pose);
}

void PurePursuit::handlePixelScale(wbb_msgs::msg::ImagePixelScale::SharedPtr scale)
{
    state_.scale = std::move(scale);
}

double PurePursuit::calculateDistance(wbb_msgs::msg::ImagePoint::SharedPtr first,
                         wbb_msgs::msg::ImagePose::SharedPtr second)
{
    return std::sqrt(std::pow(first->x - second->x, 2) +
                     std::pow(first->y - second->y, 2));
}

std::pair<double, double> PurePursuit::calculateCurvature(wbb_msgs::msg::ImagePoint::SharedPtr lookahead,
                                       wbb_msgs::msg::ImagePose::SharedPtr bot_pose, double scale)
{
    double chord = calculateDistance(lookahead, bot_pose);

    if (chord == 0 || scale == 0)
        return 0;

    double alpha = std::atan2(lookahead->y - bot_pose->y, lookahead->x - bot_pose->x) -
                   M_PI / 2 + bot_pose->theta;

    chord /= scale;
    double velocity = 0.5;
    if (std::sin(alpha) < 0)
        velocity *= -1;

    if (std::cos(alpha) >= 0)
        return {(2 * std::abs(std::sin(alpha))) / chord, velocity};
    return {-(2 * std::abs(std::sin(alpha))) / chord, velocity};
}

/*wbb_msgs::msg::ImagePoint findClosest(wbb_msgs::msg::ImagePath::SharedPtr trajectory,
                                                 wbb_msgs::msg::ImagePose::SharedPtr bot_pose)
{
    double min_dist = calculateDistance(trajectory->points[0], bot_pose);
    wbb_msgs::msg::ImagePoint min_point = trajectory->points[0];

    for (auto point : trajectory->points)
    {
        double distance = calculateDistance(point, bot_pose);
        if (distance < min_dist)
        {
            min_dist = distance;
            min_point = point;
        }
    }

    return min_point;
}*/

wbb_msgs::msg::ImagePoint::SharedPtr PurePursuit::checkSegment(wbb_msgs::msg::ImagePoint start,
                                                               wbb_msgs::msg::ImagePoint end,
                                                               wbb_msgs::msg::ImagePose::SharedPtr bot_pose,
                                                               double scale)
{
    double vector_dot_a = std::pow(end.x - start.x, 2) + std::pow(end.y - start.y, 2);

    double vector_dot_b = 2 * (start.x - bot_pose->x) * (end.x - start.x) +
                          2 * (start.y - bot_pose->y) * (end.y - start.y);

    double vector_dot_c = std::pow(start.x - bot_pose->x, 2) +
                          std::pow(start.y - bot_pose->y, 2) - std::pow(lookahead_distance / scale, 2);

    double discr = std::pow(vector_dot_b, 2) - 4 * vector_dot_a * vector_dot_c;

    if (discr < 0 || vector_dot_a == 0)
        return nullptr;

    discr = std::sqrt(discr);

    double t1 = (-vector_dot_b - discr) / (2 * vector_dot_a);
    double t2 = (-vector_dot_b + discr) / (2 * vector_dot_a);

    if (t1 >= 0 && t1 <= 1)
    {
        wbb_msgs::msg::ImagePoint pt;
        pt.x = start.x + t1 * (end.x - start.x);
        pt.y = start.y + t1 * (end.y - start.y);
        return std::make_shared<wbb_msgs::msg::ImagePoint>(pt);
    }
    if (t2 >= 0 && t2 <= 1)
    {
        wbb_msgs::msg::ImagePoint pt;
        pt.x = start.x + t2 * (end.x - start.x);
        pt.y = start.y + t2 * (end.y - start.y);
        return std::make_shared<wbb_msgs::msg::ImagePoint>(pt);
    }

    return nullptr;
}

wbb_msgs::msg::ImagePoint::SharedPtr PurePursuit::findLookahead(wbb_msgs::msg::ImagePath::SharedPtr trajectory,
                                                   wbb_msgs::msg::ImagePose::SharedPtr bot_pose, double scale)
{
    for (size_t i = trajectory_pos + 1; i < trajectory->points.size(); i++)
    {
        wbb_msgs::msg::ImagePoint::SharedPtr pt = checkSegment(trajectory->points[i - 1],trajectory->points[i],
                                                               bot_pose, scale);
        if (!pt)
            continue;

        if (state_.last_point && state_.last_point != pt)
            trajectory_pos++;

        state_.last_point = pt;
        return pt;
    }

    return nullptr;
}

void PurePursuit::visualizeLookahead(wbb_msgs::msg::ImagePoint::SharedPtr lookahead, double scale)
{
    visualization_msgs::msg::ImageMarker vis_msg;
    vis_msg.type = visualization_msgs::msg::ImageMarker::CIRCLE;
    vis_msg.ns = "0";
    vis_msg.action = visualization_msgs::msg::ImageMarker::ADD;

    std_msgs::msg::ColorRGBA color;

    color.r = 0.0;
    color.g = 0.0;
    color.b = 1.0;
    color.a = 1.0;

    vis_msg.outline_color = color;
    vis_msg.fill_color = color;
    vis_msg.filled = 1;
    vis_msg.scale = 0.1 * scale;

    geometry_msgs::msg::Point pt;
    pt.x = lookahead->x * scale;
    pt.y = lookahead->y * scale;

    vis_msg.position = pt;
    //vis_msg.lifetime = timeout_;

    signal_.visual->publish(vis_msg);
}

void PurePursuit::visualizeLARadius(wbb_msgs::msg::ImagePose::SharedPtr bot_pose, double scale)
{
    visualization_msgs::msg::ImageMarker vis_msg;
    vis_msg.type = visualization_msgs::msg::ImageMarker::CIRCLE;
    vis_msg.ns = "1";
    vis_msg.action = visualization_msgs::msg::ImageMarker::ADD;

    std_msgs::msg::ColorRGBA color;

    color.r = 0.0;
    color.g = 0.5;
    color.b = 0.5;
    color.a = 1.0;

    vis_msg.outline_color = color;
    vis_msg.filled = 0;
    vis_msg.scale = int(lookahead_distance / scale);

    geometry_msgs::msg::Point pt;
    pt.x = bot_pose->x;
    pt.y = bot_pose->y;
    vis_msg.position = pt;

    signal_.visual->publish(vis_msg);
}

void PurePursuit::visualizeRadius(double curvature, wbb_msgs::msg::ImagePose::SharedPtr bot_pose, double scale)
{
    visualization_msgs::msg::ImageMarker vis_msg;
    vis_msg.type = visualization_msgs::msg::ImageMarker::CIRCLE;
    vis_msg.ns = "2";
    vis_msg.action = visualization_msgs::msg::ImageMarker::ADD;

    double radius = 100 / scale;
    if (curvature != 0 && scale != 0)
        radius = 1 / (curvature * scale);

    geometry_msgs::msg::Point pt;
    if (radius >= 0)
    {
        double angle = bot_pose->theta + M_PI / 2;
        pt.x = int(radius * std::cos(angle)) + bot_pose->x;
        pt.y = int(radius * std::sin(angle)) + bot_pose->y;
    }
    else
    {
        double angle = bot_pose->theta - M_PI / 2;
        pt.x = bot_pose->x - int(-radius * std::cos(angle));
        pt.y = bot_pose->y - int(-radius * std::sin(angle));
    }

    vis_msg.position = pt;

    std_msgs::msg::ColorRGBA color;

    color.r = 0.0;
    color.g = 1.0;
    color.b = 0.0;
    color.a = 1.0;

    vis_msg.outline_color = color;
    vis_msg.filled = 0;
    vis_msg.scale = int(radius);

    //vis_msg.lifetime = timeout_;

    signal_.visual->publish(vis_msg);
}

void PurePursuit::sendControlCommand()
{
    auto stop = [this]()
    {
        wbb_msgs::msg::Control msg;
        msg.curvature = 0;
        msg.velocity = 0;
        signal_.control->publish(msg);
    };

    if (!(state_.trajectory && state_.bot_pose && state_.scale))
    {
        stop();
        return;
    }

    wbb_msgs::msg::ImagePose bot_pose_src = *state_.bot_pose;
    wbb_msgs::msg::ImagePose::SharedPtr bot_pose = std::make_shared<wbb_msgs::msg::ImagePose>(bot_pose_src);

    double scale = std::pow(std::cos(bot_pose->theta), 2) * state_.scale->scale_x +
                   std::pow(std::sin(bot_pose->theta), 2) * state_.scale->scale_y;

    //visualizeLARadius(bot_pose, scale);

    wbb_msgs::msg::ImagePoint::SharedPtr lh = findLookahead(state_.trajectory, bot_pose, scale);

    if (!lh)
    {
        stop();
        return;
    }

    visualizeLookahead(lh, scale);

    wbb_msgs::msg::Control msg;
    std::pair<double, double> prop = calculateCurvature(lh, bot_pose, scale);
    msg.curvature = prop.first;
    visualizeRadius(msg.curvature, bot_pose, scale);
    msg.velocity = prop.second;
    signal_.control->publish(msg);
}

} // namespace wbb