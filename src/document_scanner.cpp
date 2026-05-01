#include "document_scanner.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

cv::Point toIntPoint(const cv::Point2f& point) {
    return cv::Point(
        static_cast<int>(std::lround(point.x)),
        static_cast<int>(std::lround(point.y)));
}

double distanceBetween(const cv::Point2f& a, const cv::Point2f& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

std::vector<cv::Point2f> orderCorners(const std::vector<cv::Point>& polygon) {
    std::vector<cv::Point2f> ordered(4);

    std::array<cv::Point2f, 4> points {};
    for (std::size_t i = 0; i < 4; ++i) {
        points[i] = cv::Point2f(
            static_cast<float>(polygon[i].x),
            static_cast<float>(polygon[i].y));
    }

    auto sumComparator = [](const cv::Point2f& lhs, const cv::Point2f& rhs) {
        return (lhs.x + lhs.y) < (rhs.x + rhs.y);
    };
    auto diffComparator = [](const cv::Point2f& lhs, const cv::Point2f& rhs) {
        return (lhs.x - lhs.y) < (rhs.x - rhs.y);
    };

    ordered[0] = *std::min_element(points.begin(), points.end(), sumComparator);
    ordered[2] = *std::max_element(points.begin(), points.end(), sumComparator);
    ordered[1] = *std::max_element(points.begin(), points.end(), diffComparator);
    ordered[3] = *std::min_element(points.begin(), points.end(), diffComparator);

    return ordered;
}

bool isReasonableDocumentContour(
    const std::vector<cv::Point>& contour,
    const cv::Size& imageSize) {
    if (contour.size() != 4 || !cv::isContourConvex(contour)) {
        return false;
    }

    const double area = std::abs(cv::contourArea(contour));
    const double minArea = imageSize.area() * 0.2;
    return area >= minArea;
}

}  // namespace

ScanResult detectAndWarpDocument(
    const cv::Mat& inputImage,
    cv::Mat& warpedImage) {
    ScanResult result;

    if (inputImage.empty()) {
        result.message = "Input image is empty.";
        return result;
    }

    cv::Mat grayscale;
    cv::cvtColor(inputImage, grayscale, cv::COLOR_BGR2GRAY);

    cv::Mat blurred;
    cv::GaussianBlur(grayscale, blurred, cv::Size(5, 5), 0.0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 75.0, 200.0);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(
        edges,
        contours,
        cv::RETR_LIST,
        cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) {
        result.message = "No contours found.";
        return result;
    }

    std::sort(
        contours.begin(),
        contours.end(),
        [](const std::vector<cv::Point>& lhs, const std::vector<cv::Point>& rhs) {
            return cv::contourArea(lhs) > cv::contourArea(rhs);
        });

    std::vector<cv::Point> bestPolygon;
    double bestArea = 0.0;

    for (const auto& contour : contours) {
        const double perimeter = cv::arcLength(contour, true);
        std::vector<cv::Point> polygon;
        cv::approxPolyDP(contour, polygon, 0.02 * perimeter, true);

        if (!isReasonableDocumentContour(polygon, inputImage.size())) {
            continue;
        }

        const double area = std::abs(cv::contourArea(polygon));
        if (area > bestArea) {
            bestArea = area;
            bestPolygon = polygon;
        }
    }

    if (bestPolygon.size() != 4) {
        result.message = "Failed to detect a 4-corner document contour.";
        return result;
    }

    result.corners = orderCorners(bestPolygon);

    const double widthTop = distanceBetween(result.corners[0], result.corners[1]);
    const double widthBottom = distanceBetween(result.corners[3], result.corners[2]);
    const double heightLeft = distanceBetween(result.corners[0], result.corners[3]);
    const double heightRight = distanceBetween(result.corners[1], result.corners[2]);

    const int warpedWidth = static_cast<int>(std::max(widthTop, widthBottom));
    const int warpedHeight = static_cast<int>(std::max(heightLeft, heightRight));

    if (warpedWidth <= 0 || warpedHeight <= 0) {
        result.message = "Computed warped size is invalid.";
        return result;
    }

    const std::vector<cv::Point2f> destinationCorners = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(warpedWidth - 1), 0.0f),
        cv::Point2f(static_cast<float>(warpedWidth - 1), static_cast<float>(warpedHeight - 1)),
        cv::Point2f(0.0f, static_cast<float>(warpedHeight - 1))
    };

    const cv::Mat transform = cv::getPerspectiveTransform(
        result.corners,
        destinationCorners);

    cv::warpPerspective(
        inputImage,
        warpedImage,
        transform,
        cv::Size(warpedWidth, warpedHeight));

    result.success = true;
    result.message = "Document detected and warped successfully.";
    return result;
}

cv::Mat createMarkedPreview(
    const cv::Mat& inputImage,
    const std::vector<cv::Point2f>& corners) {
    cv::Mat preview = inputImage.clone();

    if (preview.empty() || corners.size() != 4) {
        return preview;
    }

    const cv::Scalar contourColor(0, 255, 0);
    const cv::Scalar cornerColor(0, 0, 255);
    const int lineThickness = 4;
    const int circleRadius = 10;
    const int circleThickness = -1;

    for (std::size_t i = 0; i < corners.size(); ++i) {
        const cv::Point start = toIntPoint(corners[i]);
        const cv::Point end = toIntPoint(corners[(i + 1) % corners.size()]);
        cv::line(preview, start, end, contourColor, lineThickness);
        cv::circle(preview, start, circleRadius, cornerColor, circleThickness);
    }

    return preview;
}
