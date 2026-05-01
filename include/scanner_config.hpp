#ifndef SCANNER_CONFIG_HPP
#define SCANNER_CONFIG_HPP

#include <opencv2/core.hpp>

#include <string>

struct OutputConfig {
    std::string outputDirectory;
    std::string markedSuffix;
};

struct DetectionConfig {
    int previewMaxDimension;
    double minimumDocumentAreaRatio;
    double polygonApproximationFactor;
    cv::Size gaussianBlurKernelSize;
    double gaussianBlurSigma;
    double cannyLowThreshold;
    double cannyHighThreshold;
    int thresholdMorphKernelSize;
    int borderMarginDivisor;
    int minimumBorderMarginPixels;
    double borderPenaltyPerEdge;
    double areaWeight;
    double brightnessWeight;
    double uniformityWeight;
    double fillRatioWeight;
    double brightnessStdDevClamp;
};

struct MarkerConfig {
    cv::Scalar contourColor;
    cv::Scalar textColor;
    cv::Scalar textOutlineColor;
    int lineThickness;
    int fontFace;
    double fontScale;
    int textThickness;
    int textOutlineThickness;
    cv::Point textOffset;
};

struct ScanEnhancementConfig {
    int medianBlurKernelSize;
    double adaptiveThresholdMaxValue;
    int adaptiveThresholdBlockSize;
    double adaptiveThresholdC;
    double grayscaleBlendWeight;
    double thresholdBlendWeight;
};

struct AppConfig {
    OutputConfig output;
    DetectionConfig detection;
    MarkerConfig marker;
    ScanEnhancementConfig scanEnhancement;
};

const AppConfig& getAppConfig();

#endif
