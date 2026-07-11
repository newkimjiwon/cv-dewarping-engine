#include "scanner_config.hpp"

#include <opencv2/imgproc.hpp>

namespace {

AppConfig createAppConfig() {
    AppConfig config;

    config.output.outputDirectory = "outputs";
    config.output.markedSuffix = "_marked";

    config.detection.previewMaxDimension = 1280;
    config.detection.minimumDocumentAreaRatio = 0.1;
    config.detection.polygonApproximationFactor = 0.02;
    config.detection.gaussianBlurKernelSize = cv::Size(5, 5);
    config.detection.gaussianBlurSigma = 0.0;
    config.detection.cannyLowThreshold = 30.0;
    config.detection.cannyHighThreshold = 100.0;
    config.detection.edgeDilateKernelSize = 3;
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

    config.rectification.minimumAspectRatio = 0.2;
    config.rectification.maximumAspectRatio = 5.0;
    config.rectification.parallelTolerance = 0.02;
    config.rectification.fallbackFocalFactor = 0.7;
    config.rectification.minimumFocalFactor = 0.3;
    config.rectification.maximumFocalFactor = 3.0;

    config.dewarp.enabled = true;
    config.dewarp.polynomialDegree = 3;
    config.dewarp.meshGridSize = 41;
    config.dewarp.minimumDeviationPixels = 2.5;
    config.dewarp.minimumDeviationRatio = 0.004;
    config.dewarp.maximumDeviationRatio = 0.25;

    config.scanEnhancement.backgroundDilateRatio = 0.015;
    config.scanEnhancement.backgroundBlurSigmaRatio = 0.02;
    config.scanEnhancement.sharpenAmount = 0.6;
    config.scanEnhancement.sharpenSigma = 1.2;
    config.scanEnhancement.medianBlurKernelSize = 3;
    config.scanEnhancement.adaptiveThresholdMaxValue = 255.0;
    config.scanEnhancement.adaptiveThresholdBlockSize = 41;
    config.scanEnhancement.adaptiveThresholdC = 8.0;

    config.jpegQuality = 95;

    return config;
}

const AppConfig kAppConfig = createAppConfig();

}  // namespace

const AppConfig& getAppConfig() {
    return kAppConfig;
}
