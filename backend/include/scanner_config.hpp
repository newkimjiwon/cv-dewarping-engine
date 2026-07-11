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
    int edgeDilateKernelSize;
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

// 출력 색상 모드. vFlat처럼 기본은 컬러 유지 + 조명 보정입니다.
enum class ScanColorMode {
    Color,       // 컬러 유지 + 그림자/조명 평탄화 + 샤프닝
    Grayscale,   // 흑백 + 조명 평탄화
    BlackWhite,  // 이진화 스캔 (기존 스타일)
};

struct RectificationConfig {
    // 복원된 문서 종횡비(w/h)가 이 범위를 벗어나면 픽셀 길이 기반으로 폴백
    double minimumAspectRatio;
    double maximumAspectRatio;
    // 원근이 거의 없는 경우(평행사변형 판정) 허용 오차
    double parallelTolerance;
    // 초점 거리를 추정할 수 없을 때 사용하는 가정값: f = factor * 이미지 대각선
    // (스마트폰 광각 기준 26~30mm 환산 화각에 해당)
    double fallbackFocalFactor;
    // 추정된 초점 거리가 이 범위(대각선 배수)를 벗어나면 가정값으로 대체
    double minimumFocalFactor;
    double maximumFocalFactor;
};

struct DewarpConfig {
    bool enabled;
    // 경계 곡선 편차를 근사하는 다항식 차수
    int polynomialDegree;
    // 리매핑 메시 한 변의 노드 수
    int meshGridSize;
    // 경계 휨이 이보다 작으면 평면 문서로 간주하고 투시 변환만 수행
    double minimumDeviationPixels;
    double minimumDeviationRatio;   // 변 길이 대비 비율
    // 휨이 이보다 크면 윤곽/피팅을 신뢰할 수 없다고 보고 평면 warp로 폴백
    double maximumDeviationRatio;
};

struct ScanEnhancementConfig {
    // 배경(조명) 추정: 글자를 지우는 dilate 커널 비율과 블러 시그마 비율
    double backgroundDilateRatio;
    double backgroundBlurSigmaRatio;
    // 언샤프 마스킹
    double sharpenAmount;
    double sharpenSigma;
    // BlackWhite 모드용 이진화 파라미터
    int medianBlurKernelSize;
    double adaptiveThresholdMaxValue;
    int adaptiveThresholdBlockSize;
    double adaptiveThresholdC;
};

struct AppConfig {
    OutputConfig output;
    DetectionConfig detection;
    MarkerConfig marker;
    RectificationConfig rectification;
    DewarpConfig dewarp;
    ScanEnhancementConfig scanEnhancement;
    int jpegQuality;
};

const AppConfig& getAppConfig();

#endif
