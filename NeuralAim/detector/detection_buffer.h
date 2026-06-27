#pragma once
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <opencv2/opencv.hpp>

struct DetectionBuffer
{
    std::mutex mutex;
    std::condition_variable cv;
    int version = 0;
    std::vector<cv::Rect> boxes;
    std::vector<int> classes;
    std::vector<float> confidences;
    std::chrono::steady_clock::time_point frameTimestamp{};
    std::chrono::steady_clock::time_point publishTimestamp{};

    void set(const std::vector<cv::Rect>& newBoxes, const std::vector<int>& newClasses)
    {
        set(newBoxes, newClasses, std::vector<float>(), {});
    }

    void set(
        const std::vector<cv::Rect>& newBoxes,
        const std::vector<int>& newClasses,
        const std::vector<float>& newConfidences,
        std::chrono::steady_clock::time_point newFrameTimestamp = {})
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex);
        boxes = newBoxes;
        classes = newClasses;
        confidences = newConfidences;
        frameTimestamp = (newFrameTimestamp.time_since_epoch().count() != 0) ? newFrameTimestamp : now;
        publishTimestamp = now;
        ++version;
        cv.notify_all();
    }

    void clear()
    {
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> lock(mutex);
        boxes.clear();
        classes.clear();
        confidences.clear();
        frameTimestamp = now;
        publishTimestamp = now;
        ++version;
        cv.notify_all();
    }

    void get(
        std::vector<cv::Rect>& outBoxes,
        std::vector<int>& outClasses,
        int& outVersion,
        std::chrono::steady_clock::time_point* outFrameTimestamp = nullptr,
        std::chrono::steady_clock::time_point* outPublishTimestamp = nullptr)
    {
        std::vector<float> ignoredConfidences;
        get(outBoxes, outClasses, ignoredConfidences, outVersion, outFrameTimestamp, outPublishTimestamp);
    }

    void get(
        std::vector<cv::Rect>& outBoxes,
        std::vector<int>& outClasses,
        std::vector<float>& outConfidences,
        int& outVersion,
        std::chrono::steady_clock::time_point* outFrameTimestamp = nullptr,
        std::chrono::steady_clock::time_point* outPublishTimestamp = nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex);
        outBoxes = boxes;
        outClasses = classes;
        outConfidences = confidences;
        outVersion = version;
        if (outFrameTimestamp)
            *outFrameTimestamp = frameTimestamp;
        if (outPublishTimestamp)
            *outPublishTimestamp = publishTimestamp;
    }
};
