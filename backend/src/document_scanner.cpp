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

std::vector<cv::Point2f> scaleCorners(
    const std::vector<cv::Point2f>& corners,
    const float scaleX,
    const float scaleY) {
    std::vector<cv::Point2f> scaledCorners;
    scaledCorners.reserve(corners.size());

    for (const auto& corner : corners) {
        scaledCorners.emplace_back(corner.x * scaleX, corner.y * scaleY);
    }

    return scaledCorners;
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

// 4점 근사 다각형과 함께, 곡면 보정에 쓸 원본 윤곽선도 보존합니다.
struct DocumentCandidate {
    std::vector<cv::Point> polygon;
    std::vector<cv::Point> contour;
};

void appendQuadrilateralCandidates(
    const std::vector<std::vector<cv::Point>>& contours,
    const cv::Size& imageSize,
    std::vector<DocumentCandidate>& candidates) {
    const DetectionConfig& config = getAppConfig().detection;

    for (const auto& contour : contours) {
        const double perimeter = cv::arcLength(contour, true);

        // 모서리가 둥글거나 페이지가 살짝 휜 문서는 기본 epsilon으로
        // 4점 근사가 안 나올 수 있어, 점차 키우면서 재시도합니다.
        for (double factor = config.polygonApproximationFactor;
             factor <= config.polygonApproximationFactor * 4.0;
             factor += config.polygonApproximationFactor) {
            std::vector<cv::Point> polygon;
            cv::approxPolyDP(contour, polygon, factor * perimeter, true);

            if (polygon.size() < 4) {
                break;
            }
            if (isReasonableDocumentContour(polygon, imageSize)) {
                candidates.push_back({ polygon, contour });
                break;
            }
        }
    }
}

int toOddKernelSize(const double value) {
    const int rounded = std::max(3, static_cast<int>(std::lround(value)));
    return (rounded % 2 == 0) ? rounded + 1 : rounded;
}

// 어두운 글자를 dilate로 지워 종이 배경(조명 분포)만 추정한 뒤,
// 원본을 배경으로 나눠 그림자와 조명 불균일을 평탄화합니다.
// 채널별로 수행하므로 약한 화이트 밸런스 효과도 함께 얻습니다.
cv::Mat flattenIllumination(const cv::Mat& singleChannel) {
    const ScanEnhancementConfig& config = getAppConfig().scanEnhancement;
    const int shorterSide = std::min(singleChannel.cols, singleChannel.rows);

    const int dilateKernelSize =
        toOddKernelSize(shorterSide * config.backgroundDilateRatio);
    const double blurSigma =
        std::max(3.0, shorterSide * config.backgroundBlurSigmaRatio);

    cv::Mat background;
    cv::dilate(
        singleChannel,
        background,
        cv::getStructuringElement(
            cv::MORPH_ELLIPSE,
            cv::Size(dilateKernelSize, dilateKernelSize)));
    cv::GaussianBlur(
        background,
        background,
        cv::Size(0, 0),
        blurSigma);

    cv::Mat flattened;
    cv::divide(singleChannel, background, flattened, 255.0);
    return flattened;
}

cv::Mat sharpenImage(const cv::Mat& image) {
    const ScanEnhancementConfig& config = getAppConfig().scanEnhancement;
    if (config.sharpenAmount <= 0.0) {
        return image;
    }

    cv::Mat blurred;
    cv::GaussianBlur(image, blurred, cv::Size(0, 0), config.sharpenSigma);

    cv::Mat sharpened;
    cv::addWeighted(
        image,
        1.0 + config.sharpenAmount,
        blurred,
        -config.sharpenAmount,
        0.0,
        sharpened);
    return sharpened;
}

cv::Mat enhanceScannedDocument(
    const cv::Mat& warpedColorImage,
    const ScanColorMode colorMode) {
    if (warpedColorImage.empty()) {
        return warpedColorImage;
    }

    if (colorMode == ScanColorMode::Color) {
        std::vector<cv::Mat> channels;
        cv::split(warpedColorImage, channels);
        for (auto& channel : channels) {
            channel = flattenIllumination(channel);
        }
        cv::Mat flattened;
        cv::merge(channels, flattened);
        return sharpenImage(flattened);
    }

    cv::Mat grayscale;
    cv::cvtColor(warpedColorImage, grayscale, cv::COLOR_BGR2GRAY);
    cv::Mat flattened = flattenIllumination(grayscale);

    if (colorMode == ScanColorMode::Grayscale) {
        return sharpenImage(flattened);
    }

    const ScanEnhancementConfig& config = getAppConfig().scanEnhancement;
    cv::Mat denoised;
    cv::medianBlur(flattened, denoised, config.medianBlurKernelSize);

    cv::Mat thresholded;
    cv::adaptiveThreshold(
        denoised,
        thresholded,
        config.adaptiveThresholdMaxValue,
        cv::ADAPTIVE_THRESH_GAUSSIAN_C,
        cv::THRESH_BINARY,
        config.adaptiveThresholdBlockSize,
        config.adaptiveThresholdC);
    return thresholded;
}

// Zhang-He whiteboard rectification: 4개 꼭짓점의 원근 소실 정보로부터
// 문서의 실제 종횡비(w/h)를 복원합니다. 주점은 이미지 중심으로,
// 초점 거리는 사영 기하 제약에서 직접 추정합니다.
// 복원이 불가능한(퇴화) 구도이면 0을 반환하고 호출부가 폴백합니다.
double estimateTrueAspectRatio(
    const std::vector<cv::Point2f>& corners,
    const cv::Size& imageSize) {
    const RectificationConfig& config = getAppConfig().rectification;

    const double u0 = imageSize.width / 2.0;
    const double v0 = imageSize.height / 2.0;

    // 좌표를 주점 기준으로 평행 이동한 동차 좌표.
    // 논문 표기: m1=좌상, m2=우상, m3=좌하, m4=우하
    const cv::Vec3d m1(corners[0].x - u0, corners[0].y - v0, 1.0);
    const cv::Vec3d m2(corners[1].x - u0, corners[1].y - v0, 1.0);
    const cv::Vec3d m3(corners[3].x - u0, corners[3].y - v0, 1.0);
    const cv::Vec3d m4(corners[2].x - u0, corners[2].y - v0, 1.0);

    const double denominator2 = m2.cross(m4).dot(m3);
    const double denominator3 = m3.cross(m4).dot(m2);
    if (std::abs(denominator2) < 1e-9 || std::abs(denominator3) < 1e-9) {
        return 0.0;
    }

    const double k2 = m1.cross(m4).dot(m3) / denominator2;
    const double k3 = m1.cross(m4).dot(m2) / denominator3;

    // 원근이 거의 없으면(이미 정면 촬영) 픽셀 길이 비율이 곧 실제 비율
    if (std::abs(k2 - 1.0) < config.parallelTolerance &&
        std::abs(k3 - 1.0) < config.parallelTolerance) {
        return cv::norm(m2 - m1) / cv::norm(m3 - m1);
    }

    const cv::Vec3d n2 = k2 * m2 - m1;
    const cv::Vec3d n3 = k3 * m3 - m1;

    // n2[2]=k2-1, n3[2]=k3-1. 둘 중 하나가 0에 가까우면(변이 이미지 평면과
    // 평행 — 위에서 수직으로 기울여 찍는 흔한 구도) 초점 거리를 사영 제약으로
    // 추정할 수 없으므로, 스마트폰 화각 기준 가정값을 사용합니다.
    const double diagonal = std::sqrt(
        static_cast<double>(imageSize.width) * imageSize.width +
        static_cast<double>(imageSize.height) * imageSize.height);
    const double fallbackFocalSquared =
        (config.fallbackFocalFactor * diagonal) *
        (config.fallbackFocalFactor * diagonal);

    double focalSquared = fallbackFocalSquared;
    if (std::abs(n2[2]) > config.parallelTolerance &&
        std::abs(n3[2]) > config.parallelTolerance) {
        const double estimated =
            -(n2[0] * n3[0] + n2[1] * n3[1]) / (n2[2] * n3[2]);
        const double minFocalSquared =
            (config.minimumFocalFactor * diagonal) *
            (config.minimumFocalFactor * diagonal);
        const double maxFocalSquared =
            (config.maximumFocalFactor * diagonal) *
            (config.maximumFocalFactor * diagonal);
        if (std::isfinite(estimated) &&
            estimated >= minFocalSquared &&
            estimated <= maxFocalSquared) {
            focalSquared = estimated;
        }
    }

    const double widthTerm =
        (n2[0] * n2[0] + n2[1] * n2[1]) / focalSquared + n2[2] * n2[2];
    const double heightTerm =
        (n3[0] * n3[0] + n3[1] * n3[1]) / focalSquared + n3[2] * n3[2];
    if (widthTerm <= 0.0 || heightTerm <= 0.0) {
        return 0.0;
    }

    const double ratio = std::sqrt(widthTerm / heightTerm);
    return std::isfinite(ratio) ? ratio : 0.0;
}

// --- 곡면(페이지 휨) 보정 -------------------------------------------------
//
// 투시 변환 H로 문서 윤곽선을 직교 공간으로 옮기면, 페이지가 평평했다면
// 네 경계가 출력 사각형의 변과 일치해야 합니다. 책처럼 페이지가 불룩하면
// 경계가 곡선 편차로 나타나므로, 편차를 다항식으로 근사한 뒤 Coons patch
// 메시로 remap 해서 폅니다.

std::vector<double> fitPolynomialLeastSquares(
    const std::vector<double>& ts,
    const std::vector<double>& values,
    const int degree) {
    const int rows = static_cast<int>(ts.size());
    const int cols = degree + 1;
    if (rows < cols + 1) {
        return {};
    }

    cv::Mat vandermonde(rows, cols, CV_64F);
    cv::Mat rhs(rows, 1, CV_64F);
    for (int r = 0; r < rows; ++r) {
        double power = 1.0;
        for (int c = 0; c < cols; ++c) {
            vandermonde.at<double>(r, c) = power;
            power *= ts[r];
        }
        rhs.at<double>(r, 0) = values[r];
    }

    cv::Mat solution;
    if (!cv::solve(vandermonde, rhs, solution, cv::DECOMP_SVD)) {
        return {};
    }

    std::vector<double> coefficients(cols);
    for (int c = 0; c < cols; ++c) {
        coefficients[c] = solution.at<double>(c, 0);
    }
    return coefficients;
}

double evaluatePolynomial(const std::vector<double>& coefficients, const double t) {
    double value = 0.0;
    double power = 1.0;
    for (const double coefficient : coefficients) {
        value += coefficient * power;
        power *= t;
    }
    return value;
}

int nearestContourIndex(
    const std::vector<cv::Point2f>& contour,
    const cv::Point2f& target) {
    int bestIndex = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < contour.size(); ++i) {
        const double distance = distanceBetween(contour[i], target);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

// 윤곽선을 코너 4개 기준으로 4개의 경계 폴리라인으로 자릅니다.
// edges[0]=상단(TL→TR), [1]=우측(TR→BR), [2]=하단(BR→BL), [3]=좌측(BL→TL)
bool splitContourIntoEdges(
    const std::vector<cv::Point2f>& contour,
    const std::vector<cv::Point2f>& corners,
    std::array<std::vector<cv::Point2f>, 4>& edges) {
    const int pointCount = static_cast<int>(contour.size());
    if (pointCount < 8 || corners.size() != 4) {
        return false;
    }

    std::array<int, 4> cornerIndices {};
    for (int corner = 0; corner < 4; ++corner) {
        cornerIndices[corner] = nearestContourIndex(contour, corners[corner]);
    }

    std::array<int, 4> visitOrder = { 0, 1, 2, 3 };
    std::sort(
        visitOrder.begin(),
        visitOrder.end(),
        [&cornerIndices](const int lhs, const int rhs) {
            return cornerIndices[lhs] < cornerIndices[rhs];
        });

    // 윤곽선을 따라가면 코너가 0→1→2→3 또는 역순으로 나와야 정상입니다.
    const int direction = (visitOrder[1] - visitOrder[0] + 4) % 4;
    if (direction != 1 && direction != 3) {
        return false;
    }
    for (int k = 0; k < 4; ++k) {
        const int current = visitOrder[k];
        const int next = visitOrder[(k + 1) % 4];
        if ((next - current + 4) % 4 != direction && k < 3) {
            return false;
        }
    }

    for (int k = 0; k < 4; ++k) {
        const int cornerA = visitOrder[k];
        const int cornerB = visitOrder[(k + 1) % 4];
        const int edgeId = (direction == 1) ? cornerA : cornerB;

        std::vector<cv::Point2f> arc;
        int index = cornerIndices[cornerA];
        while (true) {
            arc.push_back(contour[index]);
            if (index == cornerIndices[cornerB]) {
                break;
            }
            index = (index + 1) % pointCount;
            if (static_cast<int>(arc.size()) > pointCount) {
                return false;
            }
        }
        edges[edgeId] = std::move(arc);
    }
    return true;
}

// 직교 공간의 경계 폴리라인에서 기준선(base) 대비 편차 다항식을 피팅합니다.
// horizontal=true면 t는 x/length, 편차는 y-base입니다.
std::vector<double> fitEdgeDeviation(
    const std::vector<cv::Point2f>& rectifiedArc,
    const bool horizontal,
    const double base,
    const double length,
    double& maxDeviation) {
    maxDeviation = 0.0;
    if (length <= 1.0) {
        return {};
    }

    const DewarpConfig& config = getAppConfig().dewarp;
    std::vector<double> ts;
    std::vector<double> deviations;
    ts.reserve(rectifiedArc.size());
    deviations.reserve(rectifiedArc.size());

    for (const auto& point : rectifiedArc) {
        const double t = (horizontal ? point.x : point.y) / length;
        if (t < -0.05 || t > 1.05) {
            continue;
        }
        ts.push_back(std::min(1.0, std::max(0.0, t)));
        deviations.push_back((horizontal ? point.y : point.x) - base);
    }

    std::vector<double> coefficients =
        fitPolynomialLeastSquares(ts, deviations, config.polynomialDegree);
    if (coefficients.empty()) {
        return {};
    }

    // 코너는 투시 변환으로 정확히 사각형 꼭짓점에 매핑되므로,
    // 곡선도 양 끝에서 편차 0을 지나도록 선형 보정합니다.
    const double startDeviation = evaluatePolynomial(coefficients, 0.0);
    const double endDeviation = evaluatePolynomial(coefficients, 1.0);
    coefficients[0] -= startDeviation;
    if (coefficients.size() > 1) {
        coefficients[1] -= (endDeviation - startDeviation);
    }

    for (int i = 0; i <= 32; ++i) {
        const double deviation =
            std::abs(evaluatePolynomial(coefficients, i / 32.0));
        maxDeviation = std::max(maxDeviation, deviation);
    }
    return coefficients;
}

bool tryCurvedDewarp(
    const cv::Mat& inputImage,
    const std::vector<cv::Point2f>& documentContour,
    const std::vector<cv::Point2f>& corners,
    const cv::Mat& transform,
    const cv::Size& outputSize,
    cv::Mat& warpedColorImage) {
    const DewarpConfig& config = getAppConfig().dewarp;
    if (!config.enabled || documentContour.size() < 8) {
        return false;
    }

    std::array<std::vector<cv::Point2f>, 4> edges;
    if (!splitContourIntoEdges(documentContour, corners, edges)) {
        return false;
    }

    const double width = outputSize.width - 1.0;
    const double height = outputSize.height - 1.0;

    std::array<std::vector<double>, 4> deviationPolys;
    double maxDeviation = 0.0;
    const std::array<bool, 4> isHorizontal = { true, false, true, false };
    const std::array<double, 4> bases = { 0.0, width, height, 0.0 };
    const std::array<double, 4> lengths = { width, height, width, height };

    for (int edgeId = 0; edgeId < 4; ++edgeId) {
        std::vector<cv::Point2f> rectifiedArc;
        cv::perspectiveTransform(edges[edgeId], rectifiedArc, transform);

        double edgeDeviation = 0.0;
        deviationPolys[edgeId] = fitEdgeDeviation(
            rectifiedArc,
            isHorizontal[edgeId],
            bases[edgeId],
            lengths[edgeId],
            edgeDeviation);
        maxDeviation = std::max(maxDeviation, edgeDeviation);
    }

    const double shorterSide = std::min(outputSize.width, outputSize.height);
    const double minimumDeviation = std::max(
        config.minimumDeviationPixels,
        config.minimumDeviationRatio * shorterSide);
    if (maxDeviation < minimumDeviation ||
        maxDeviation > config.maximumDeviationRatio * shorterSide) {
        return false;
    }

    // Coons patch: 편차 항만 남는 형태로 단순화됩니다.
    //   S(u,v) = (u·W + (1-u)·devL(v) + u·devR(v),
    //             v·H + (1-v)·devT(u) + v·devB(u))
    const int gridSize = std::max(4, config.meshGridSize);
    std::vector<cv::Point2f> rectifiedNodes;
    rectifiedNodes.reserve(
        static_cast<std::size_t>(gridSize) * gridSize);

    for (int row = 0; row < gridSize; ++row) {
        const double v = static_cast<double>(row) / (gridSize - 1);
        const double devLeft = evaluatePolynomial(deviationPolys[3], v);
        const double devRight = evaluatePolynomial(deviationPolys[1], v);
        for (int col = 0; col < gridSize; ++col) {
            const double u = static_cast<double>(col) / (gridSize - 1);
            const double devTop = evaluatePolynomial(deviationPolys[0], u);
            const double devBottom = evaluatePolynomial(deviationPolys[2], u);
            rectifiedNodes.emplace_back(
                static_cast<float>(u * width + (1.0 - u) * devLeft + u * devRight),
                static_cast<float>(v * height + (1.0 - v) * devTop + v * devBottom));
        }
    }

    std::vector<cv::Point2f> sourceNodes;
    cv::perspectiveTransform(rectifiedNodes, sourceNodes, transform.inv());

    cv::Mat mapSmallX(gridSize, gridSize, CV_32F);
    cv::Mat mapSmallY(gridSize, gridSize, CV_32F);
    for (int row = 0; row < gridSize; ++row) {
        for (int col = 0; col < gridSize; ++col) {
            const cv::Point2f& node =
                sourceNodes[static_cast<std::size_t>(row) * gridSize + col];
            mapSmallX.at<float>(row, col) = node.x;
            mapSmallY.at<float>(row, col) = node.y;
        }
    }

    cv::Mat mapX;
    cv::Mat mapY;
    cv::resize(mapSmallX, mapX, outputSize, 0.0, 0.0, cv::INTER_LINEAR);
    cv::resize(mapSmallY, mapY, outputSize, 0.0, 0.0, cv::INTER_LINEAR);

    cv::remap(
        inputImage,
        warpedColorImage,
        mapX,
        mapY,
        cv::INTER_CUBIC,
        cv::BORDER_REPLICATE);
    return true;
}

bool detectDocumentCornersFromImage(
    const cv::Mat& image,
    std::vector<cv::Point2f>& corners,
    std::vector<cv::Point2f>& documentContour) {
    if (image.empty()) {
        return false;
    }

    cv::Mat grayscale;
    cv::cvtColor(image, grayscale, cv::COLOR_BGR2GRAY);

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
    // 저대비 외곽선이 군데군데 끊기면 4점 윤곽이 안 나오므로 틈을 메웁니다.
    cv::dilate(
        edges,
        edges,
        cv::getStructuringElement(
            cv::MORPH_RECT,
            cv::Size(
                config.edgeDilateKernelSize,
                config.edgeDilateKernelSize)));

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
        return false;
    }

    const DocumentCandidate* bestCandidate = nullptr;
    double bestScore = std::numeric_limits<double>::lowest();
    std::vector<DocumentCandidate> candidates;
    candidates.reserve(edgeContours.size() + thresholdContours.size());

    appendQuadrilateralCandidates(
        thresholdContours,
        image.size(),
        candidates);
    appendQuadrilateralCandidates(
        edgeContours,
        image.size(),
        candidates);

    for (const auto& candidate : candidates) {
        const double score = scoreDocumentContour(
            candidate.polygon,
            grayscale,
            image.size());
        if (score > bestScore) {
            bestScore = score;
            bestCandidate = &candidate;
        }
    }

    if (bestCandidate == nullptr || bestCandidate->polygon.size() != 4) {
        return false;
    }

    corners = orderCorners(bestCandidate->polygon);
    documentContour.clear();
    documentContour.reserve(bestCandidate->contour.size());
    for (const auto& point : bestCandidate->contour) {
        documentContour.emplace_back(
            static_cast<float>(point.x),
            static_cast<float>(point.y));
    }
    return true;
}

bool detectDocumentCornersPreview(
    const cv::Mat& inputImage,
    std::vector<cv::Point2f>& corners,
    std::vector<cv::Point2f>& documentContour) {
    const DetectionConfig& config = getAppConfig().detection;
    const int longestSide = std::max(inputImage.cols, inputImage.rows);

    if (longestSide <= config.previewMaxDimension) {
        return detectDocumentCornersFromImage(inputImage, corners, documentContour);
    }

    const double previewScale =
        static_cast<double>(config.previewMaxDimension) /
        static_cast<double>(longestSide);

    const int previewWidth = std::max(
        1,
        static_cast<int>(std::lround(inputImage.cols * previewScale)));
    const int previewHeight = std::max(
        1,
        static_cast<int>(std::lround(inputImage.rows * previewScale)));

    cv::Mat previewImage;
    cv::resize(
        inputImage,
        previewImage,
        cv::Size(previewWidth, previewHeight),
        0.0,
        0.0,
        cv::INTER_AREA);

    std::vector<cv::Point2f> previewCorners;
    std::vector<cv::Point2f> previewContour;
    if (!detectDocumentCornersFromImage(
            previewImage, previewCorners, previewContour)) {
        return false;
    }

    const float scaleX =
        static_cast<float>(inputImage.cols) / static_cast<float>(previewWidth);
    const float scaleY =
        static_cast<float>(inputImage.rows) / static_cast<float>(previewHeight);
    corners = scaleCorners(previewCorners, scaleX, scaleY);
    documentContour = scaleCorners(previewContour, scaleX, scaleY);
    return true;
}

}  // namespace

ScanResult detectAndWarpDocument(
    const cv::Mat& inputImage,
    cv::Mat& warpedImage,
    const ScanColorMode colorMode) {
    ScanResult result;

    if (inputImage.empty()) {
        result.message = "Input image is empty.";
        return result;
    }

    std::vector<cv::Point2f> documentContour;
    if (!detectDocumentCornersPreview(
            inputImage, result.corners, documentContour)) {
        result.message = "Failed to detect a 4-corner document contour.";
        return result;
    }

    const double widthTop = distanceBetween(result.corners[0], result.corners[1]);
    const double widthBottom = distanceBetween(result.corners[3], result.corners[2]);
    const double heightLeft = distanceBetween(result.corners[0], result.corners[3]);
    const double heightRight = distanceBetween(result.corners[1], result.corners[2]);

    const double measuredWidth = std::max(widthTop, widthBottom);
    const double measuredHeight = std::max(heightLeft, heightRight);

    if (measuredWidth <= 1.0 || measuredHeight <= 1.0) {
        result.message = "Computed warped size is invalid.";
        return result;
    }

    // 픽셀 길이 비율은 촬영 각도에 따라 왜곡되므로,
    // 원근 정보로 복원한 실제 종횡비를 우선 사용합니다.
    const RectificationConfig& rectConfig = getAppConfig().rectification;
    double aspectRatio = estimateTrueAspectRatio(result.corners, inputImage.size());
    if (aspectRatio < rectConfig.minimumAspectRatio ||
        aspectRatio > rectConfig.maximumAspectRatio) {
        aspectRatio = measuredWidth / measuredHeight;
    }

    // 측정된 픽셀 면적을 유지하면서 복원된 비율로 출력 크기를 정해
    // 불필요한 업스케일/다운스케일을 피합니다.
    const double measuredArea = measuredWidth * measuredHeight;
    const int warpedWidth = std::max(
        1, static_cast<int>(std::lround(std::sqrt(measuredArea * aspectRatio))));
    const int warpedHeight = std::max(
        1, static_cast<int>(std::lround(std::sqrt(measuredArea / aspectRatio))));

    const std::vector<cv::Point2f> destinationCorners = {
        cv::Point2f(0.0f, 0.0f),
        cv::Point2f(static_cast<float>(warpedWidth - 1), 0.0f),
        cv::Point2f(static_cast<float>(warpedWidth - 1), static_cast<float>(warpedHeight - 1)),
        cv::Point2f(0.0f, static_cast<float>(warpedHeight - 1))
    };

    const cv::Mat transform = cv::getPerspectiveTransform(
        result.corners,
        destinationCorners);

    // 경계가 휘어 있으면(펼친 책 등) 메시 기반 곡면 보정을 먼저 시도하고,
    // 평면 문서이거나 곡선 피팅이 불안정하면 투시 변환만 수행합니다.
    cv::Mat warpedColorImage;
    if (!tryCurvedDewarp(
            inputImage,
            documentContour,
            result.corners,
            transform,
            cv::Size(warpedWidth, warpedHeight),
            warpedColorImage)) {
        cv::warpPerspective(
            inputImage,
            warpedColorImage,
            transform,
            cv::Size(warpedWidth, warpedHeight),
            cv::INTER_CUBIC);
    }

    warpedImage = enhanceScannedDocument(warpedColorImage, colorMode);

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
