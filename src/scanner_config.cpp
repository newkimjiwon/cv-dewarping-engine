#include "scanner_config.hpp"

#include <opencv2/imgproc.hpp>

namespace {

AppConfig createAppConfig() {
    AppConfig config;

    config.output.outputDirectory = "outputs";
    config.output.markedSuffix = "_marked";

    config.detection.minimumDocumentAreaRatio = 0.2;
    config.detection.polygonApproximationFactor = 0.02;
    config.detection.gaussianBlurKernelSize = cv::Size(5, 5);
    config.detection.gaussianBlurSigma = 0.0;
    config.detection.cannyLowThreshold = 75.0;
    config.detection.cannyHighThreshold = 200.0;
    config.detection.thresholdMorphKernelSize = 5;
    config.detection.borderMarginDivisor = 50;
    config.detection.minimumBorderMarginPixels = 12;
    config.detection.borderPenaltyPerEdge = 0.15;
    config.detection.areaWeight = 0.35;
    config.detection.brightnessWeight = 0.35;
    config.detection.uniformityWeight = 0.20;
    config.detection.fillRatioWeight = 0.10;
    config.detection.brightnessStdDevClamp = 128.0;

    config.marker.contourColor = cv::Scalar(0, 255, 0);
    config.marker.textColor = cv::Scalar(255, 255, 255);
    config.marker.textOutlineColor = cv::Scalar(0, 0, 0);
    config.marker.lineThickness = 2;
    config.marker.fontFace = cv::FONT_HERSHEY_SIMPLEX;
    config.marker.fontScale = 0.5;
    config.marker.textThickness = 1;
    config.marker.textOutlineThickness = 3;
    config.marker.textOffset = cv::Point(8, -8);

    config.scanEnhancement.medianBlurKernelSize = 1;
    config.scanEnhancement.adaptiveThresholdMaxValue = 255.0;
    config.scanEnhancement.adaptiveThresholdBlockSize = 41;
    config.scanEnhancement.adaptiveThresholdC = 1.5;
    config.scanEnhancement.grayscaleBlendWeight = 0.96;
    config.scanEnhancement.thresholdBlendWeight = 0.04;

    return config;
}

const AppConfig kAppConfig = createAppConfig();

}  // namespace

const AppConfig& getAppConfig() {
    return kAppConfig;
}
