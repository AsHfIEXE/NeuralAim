#ifndef CIRCLE_FOV_H
#define CIRCLE_FOV_H

#include <algorithm>

#include <opencv2/core.hpp>

inline int clampCircleFovRadiusPercent(int radiusPercent)
{
    return std::clamp(radiusPercent, 1, 100);
}

inline float getCircleFovRadiusPixels(const cv::Size& size, int radiusPercent)
{
    const float maxRadius = static_cast<float>(std::min(size.width, size.height)) * 0.5f;
    return maxRadius * (static_cast<float>(clampCircleFovRadiusPercent(radiusPercent)) / 100.0f);
}

inline cv::Point2f getCircleFovCenter(const cv::Size& size)
{
    return cv::Point2f(static_cast<float>(size.width) * 0.5f, static_cast<float>(size.height) * 0.5f);
}

inline bool pointInsideCircleFov(const cv::Point2f& point, const cv::Size& size, int radiusPercent)
{
    if (size.width <= 0 || size.height <= 0)
        return true;

    const cv::Point2f center = getCircleFovCenter(size);
    const float radius = getCircleFovRadiusPixels(size, radiusPercent);
    const float dx = point.x - center.x;
    const float dy = point.y - center.y;
    return dx * dx + dy * dy <= radius * radius;
}

#endif // CIRCLE_FOV_H
