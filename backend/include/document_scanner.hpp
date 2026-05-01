#ifndef DOCUMENT_SCANNER_HPP
#define DOCUMENT_SCANNER_HPP

#include <opencv2/core.hpp>

#include <string>
#include <vector>

struct ScanResult {
    bool success = false;
    std::string message;
    std::vector<cv::Point2f> corners;
};

ScanResult detectAndWarpDocument(
    const cv::Mat& inputImage,
    cv::Mat& warpedImage);

cv::Mat createMarkedPreview(
    const cv::Mat& inputImage,
    const std::vector<cv::Point2f>& corners);

#endif
