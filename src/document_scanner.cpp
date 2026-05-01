#include "document_scanner.hpp"
#include "scanner_config.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>

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
    const DetectionConfig& config = getAppConfig().detection;

    if (contour.size() != 4 || !cv::isContourConvex(contour)) {
        return false;
    }

    const double area = std::abs(cv::contourArea(contour));
    const double minArea = imageSize.area() * config.minimumDocumentAreaRatio;
    return area >= minArea;
}

double calculateBorderPenalty(
    const cv::Rect& boundingRect,
    const cv::Size& imageSize) {
    const DetectionConfig& config = getAppConfig().detection;
    const int marginX = std::max(
        config.minimumBorderMarginPixels,
        imageSize.width / config.borderMarginDivisor);
    const int marginY = std::max(
        config.minimumBorderMarginPixels,
        imageSize.height / config.borderMarginDivisor);

    double penalty = 0.0;
    if (boundingRect.x <= marginX) {
        penalty += config.borderPenaltyPerEdge;
    }
    if (boundingRect.y <= marginY) {
        penalty += config.borderPenaltyPerEdge;
    }
    if (boundingRect.x + boundingRect.width >= imageSize.width - marginX) {
        penalty += config.borderPenaltyPerEdge;
    }
    if (boundingRect.y + boundingRect.height >= imageSize.height - marginY) {
        penalty += config.borderPenaltyPerEdge;
    }

    return penalty;
}

double scoreDocumentContour(
    const std::vector<cv::Point>& contour,
    const cv::Mat& grayscale,
    const cv::Size& imageSize) {
    const DetectionConfig& config = getAppConfig().detection;
    const double imageArea = static_cast<double>(imageSize.area());
    const double contourArea = std::abs(cv::contourArea(contour));
    const double areaScore = contourArea / imageArea;

    cv::Mat mask = cv::Mat::zeros(imageSize, CV_8UC1);
    std::vector<std::vector<cv::Point>> polygons = { contour };
    cv::fillPoly(mask, polygons, cv::Scalar(255));

    cv::Scalar meanBrightness;
    cv::Scalar stddevBrightness;
    cv::meanStdDev(grayscale, meanBrightness, stddevBrightness, mask);

    const double brightnessScore = meanBrightness[0] / 255.0;
    const double uniformityScore = 1.0 - std::min(
        stddevBrightness[0] / config.brightnessStdDevClamp,
        1.0);

    const cv::Rect boundingRect = cv::boundingRect(contour);
    const double fillRatio =
        contourArea / static_cast<double>(boundingRect.area());
    const double borderPenalty = calculateBorderPenalty(boundingRect, imageSize);

    return (areaScore * config.areaWeight) +
           (brightnessScore * config.brightnessWeight) +
           (uniformityScore * config.uniformityWeight) +
           (fillRatio * config.fillRatioWeight) -
           borderPenalty;
}

void appendQuadrilateralCandidates(
    const std::vector<std::vector<cv::Point>>& contours,
    const cv::Size& imageSize,
    std::vector<std::vector<cv::Point>>& candidates) {
    const DetectionConfig& config = getAppConfig().detection;

    for (const auto& contour : contours) {
        const double perimeter = cv::arcLength(contour, true);
        std::vector<cv::Point> polygon;
        cv::approxPolyDP(
            contour,
            polygon,
            config.polygonApproximationFactor * perimeter,
            true);

        if (isReasonableDocumentContour(polygon, imageSize)) {
            candidates.push_back(polygon);
        }
    }
}

cv::Mat enhanceScannedDocument(const cv::Mat& warpedColorImage) {
    if (warpedColorImage.empty()) {
        return warpedColorImage;
    }

    const ScanEnhancementConfig& config = getAppConfig().scanEnhancement;

    cv::Mat grayscale;
    cv::cvtColor(warpedColorImage, grayscale, cv::COLOR_BGR2GRAY);

    cv::Mat denoised;
    cv::medianBlur(grayscale, denoised, config.medianBlurKernelSize);

    cv::Mat thresholded;
    cv::adaptiveThreshold(
        denoised,
        thresholded,
        config.adaptiveThresholdMaxValue,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY,
        config.adaptiveThresholdBlockSize,
        config.adaptiveThresholdC);

    cv::Mat softenedScan;
    cv::addWeighted(
        denoised,
        config.grayscaleBlendWeight,
        thresholded,
        config.thresholdBlendWeight,
        0.0,
        softenedScan);

    return softenedScan;
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

    const DetectionConfig& config = getAppConfig().detection;

    cv::Mat blurred;
    cv::GaussianBlur(
        grayscale,
        blurred,
        config.gaussianBlurKernelSize,
        config.gaussianBlurSigma);

    cv::Mat edges;
    cv::Canny(
        blurred,
        edges,
        config.cannyLowThreshold,
        config.cannyHighThreshold);

    cv::Mat thresholded;
    cv::threshold(
        blurred,
        thresholded,
        0.0,
        255.0,
        cv::THRESH_BINARY + cv::THRESH_OTSU);
    cv::morphologyEx(
        thresholded,
        thresholded,
        cv::MORPH_CLOSE,
        cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(
                config.thresholdMorphKernelSize,
                config.thresholdMorphKernelSize)));

    std::vector<std::vector<cv::Point>> edgeContours;
    cv::findContours(
        edges,
        edgeContours,
        cv::RETR_LIST,
        cv::CHAIN_APPROX_SIMPLE);

    std::vector<std::vector<cv::Point>> thresholdContours;
    cv::findContours(
        thresholded,
        thresholdContours,
        cv::RETR_EXTERNAL,
        cv::CHAIN_APPROX_SIMPLE);

    if (edgeContours.empty() && thresholdContours.empty()) {
        result.message = "No contours found.";
        return result;
    }

    std::vector<cv::Point> bestPolygon;
    double bestScore = std::numeric_limits<double>::lowest();
    std::vector<std::vector<cv::Point>> candidates;
    candidates.reserve(edgeContours.size() + thresholdContours.size());

    appendQuadrilateralCandidates(
        thresholdContours,
        inputImage.size(),
        candidates);
    appendQuadrilateralCandidates(
        edgeContours,
        inputImage.size(),
        candidates);

    for (const auto& candidate : candidates) {
        const double score = scoreDocumentContour(
            candidate,
            grayscale,
            inputImage.size());
        if (score > bestScore) {
            bestScore = score;
            bestPolygon = candidate;
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

    cv::Mat warpedColorImage;
    cv::warpPerspective(
        inputImage,
        warpedColorImage,
        transform,
        cv::Size(warpedWidth, warpedHeight));

    warpedImage = enhanceScannedDocument(warpedColorImage);

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

    const MarkerConfig& config = getAppConfig().marker;

    for (std::size_t i = 0; i < corners.size(); ++i) {
        const cv::Point start = toIntPoint(corners[i]);
        const cv::Point end = toIntPoint(corners[(i + 1) % corners.size()]);
        cv::line(preview, start, end, config.contourColor, config.lineThickness);

        std::ostringstream label;
        label << "("
              << static_cast<int>(std::lround(corners[i].x))
              << ", "
              << static_cast<int>(std::lround(corners[i].y))
              << ")";

        const cv::Point textOrigin(
            start.x + config.textOffset.x,
            start.y + config.textOffset.y);
        cv::putText(
            preview,
            label.str(),
            textOrigin,
            config.fontFace,
            config.fontScale,
            config.textOutlineColor,
            config.textOutlineThickness,
            cv::LINE_AA);
        cv::putText(
            preview,
            label.str(),
            textOrigin,
            config.fontFace,
            config.fontScale,
            config.textColor,
            config.textThickness,
            cv::LINE_AA);
    }

    return preview;
}
