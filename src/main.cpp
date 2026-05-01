#include "document_scanner.hpp"
#include "scanner_config.hpp"

#include <opencv2/imgcodecs.hpp>

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string escapeJson(const std::string& input) {
    std::ostringstream escaped;
    for (const char ch : input) {
        switch (ch) {
            case '\\':
                escaped << "\\\\";
                break;
            case '"':
                escaped << "\\\"";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                escaped << ch;
                break;
        }
    }
    return escaped.str();
}

std::string toJson(
    const ScanResult& result,
    const std::string& outputPath,
    const std::string& markedOutputPath) {
    std::ostringstream json;
    json << std::boolalpha;
    json << "{";
    json << "\"success\":" << result.success << ",";
    json << "\"message\":\"" << escapeJson(result.message) << "\",";
    json << "\"output_path\":\"" << escapeJson(outputPath) << "\",";
    json << "\"marked_output_path\":\"" << escapeJson(markedOutputPath) << "\",";
    json << "\"corners\":[";

    for (std::size_t i = 0; i < result.corners.size(); ++i) {
        const auto& point = result.corners[i];
        json << "{"
             << "\"x\":" << std::fixed << std::setprecision(2) << point.x << ","
             << "\"y\":" << std::fixed << std::setprecision(2) << point.y
             << "}";
        if (i + 1 < result.corners.size()) {
            json << ",";
        }
    }

    json << "]";
    json << "}";
    return json.str();
}

void printUsage(const char* executableName) {
    std::cerr << "Usage: " << executableName << " <input-image-path>\n";
}

std::string buildOutputPath(
    const std::string& inputPath,
    const OutputConfig& outputConfig,
    bool marked) {
    const std::filesystem::path inputFilePath(inputPath);
    const std::string stem = inputFilePath.stem().string();
    const std::string extension = inputFilePath.has_extension()
        ? inputFilePath.extension().string()
        : ".jpg";

    std::filesystem::path outputFilePath(outputConfig.outputDirectory);
    if (marked) {
        outputFilePath /= stem + outputConfig.markedSuffix + extension;
    } else {
        outputFilePath /= stem + extension;
    }

    return outputFilePath.string();
}

}  // namespace

int main(int argc, char** argv) {
    const OutputConfig& outputConfig = getAppConfig().output;

    if (argc != 2) {
        printUsage(argv[0]);
        ScanResult invalidArgs;
        invalidArgs.message = "Exactly one input image path is required.";
        std::cout << toJson(invalidArgs, "", "") << '\n';
        return EXIT_FAILURE;
    }

    const std::string inputPath = argv[1];
    const std::string outputPath = buildOutputPath(inputPath, outputConfig, false);
    const std::string markedOutputPath = buildOutputPath(inputPath, outputConfig, true);
    const cv::Mat inputImage = cv::imread(inputPath, cv::IMREAD_COLOR);

    if (inputImage.empty()) {
        ScanResult loadFailure;
        loadFailure.message = "Failed to load input image: " + inputPath;
        std::cout << toJson(loadFailure, outputPath, markedOutputPath) << '\n';
        return EXIT_FAILURE;
    }

    cv::Mat warpedImage;
    ScanResult result = detectAndWarpDocument(inputImage, warpedImage);

    if (result.success) {
        std::filesystem::create_directories(outputConfig.outputDirectory);
        const cv::Mat markedImage = createMarkedPreview(inputImage, result.corners);

        if (!cv::imwrite(outputPath, warpedImage)) {
            result.success = false;
            result.message = "Document was detected, but saving failed: " + outputPath;
        } else if (markedImage.empty() || !cv::imwrite(markedOutputPath, markedImage)) {
            result.success = false;
            result.message = "Document was detected, but saving failed: " + markedOutputPath;
        }
    }

    std::cout << toJson(result, outputPath, markedOutputPath) << '\n';
    return result.success ? EXIT_SUCCESS : EXIT_FAILURE;
}
