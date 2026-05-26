#include "roadscript/watermark/haar.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {
    using roadscript::watermark::HaarBands;
    using roadscript::watermark::haar_dwt2_level1;
    using roadscript::watermark::haar_idwt2_level1;

    cv::Mat makeSequentialMatrix(int rows, int cols) {
        cv::Mat out(rows, cols, CV_32F);
        float value = 0.0f;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                out.at<float>(r, c) = value;
                value += 1.0f;
            }
        }

        return out;
    }

    bool matsEqualExact(const cv::Mat &actual, const cv::Mat &expected) {
        if (actual.rows != expected.rows || actual.cols != expected.cols || actual.type() != expected.type()) {
            return false;
        }

        for (int r = 0; r < actual.rows; ++r) {
            for (int c = 0; c < actual.cols; ++c) {
                if (actual.at<float>(r, c) != expected.at<float>(r, c)) {
                    return false;
                }
            }
        }

        return true;
    }

    cv::Mat matFromRows(std::initializer_list<std::initializer_list<float>> rows) {
        const int rowCount = static_cast<int>(rows.size());
        const int colCount = rowCount == 0 ? 0 : static_cast<int>(rows.begin()->size());

        cv::Mat out(rowCount, colCount, CV_32F);
        int r = 0;
        for (const auto &row: rows) {
            int c = 0;
            for (float value: row) {
                out.at<float>(r, c) = value;
                ++c;
            }
            ++r;
        }
        return out;
    }

    void printMat(const std::string &label, const cv::Mat &mat) {
        std::cout << label << " (" << mat.rows << "x" << mat.cols << ")\n";
        for (int r = 0; r < mat.rows; ++r) {
            std::cout << "  ";
            for (int c = 0; c < mat.cols; ++c) {
                if (c > 0) {
                    std::cout << ", ";
                }
                std::cout << mat.at<float>(r, c);
            }
            std::cout << "\n";
        }
    }

    bool verifyCase(
        const std::string &label,
        const cv::Mat &input,
        const cv::Mat &expectedLL,
        const cv::Mat &expectedLH,
        const cv::Mat &expectedHL,
        const cv::Mat &expectedHH,
        int expectedPadH,
        int expectedPadW
    ) {
        const HaarBands bands = haar_dwt2_level1(input);
        bool ok = true;

        if (!matsEqualExact(bands.LL, expectedLL)) {
            std::cerr << "[FAIL] " << label << ": LL mismatch\n";
            ok = false;
        }
        if (!matsEqualExact(bands.LH, expectedLH)) {
            std::cerr << "[FAIL] " << label << ": LH mismatch\n";
            ok = false;
        }
        if (!matsEqualExact(bands.HL, expectedHL)) {
            std::cerr << "[FAIL] " << label << ": HL mismatch\n";
            ok = false;
        }
        if (!matsEqualExact(bands.HH, expectedHH)) {
            std::cerr << "[FAIL] " << label << ": HH mismatch\n";
            ok = false;
        }
        if (bands.pad_h != expectedPadH || bands.pad_w != expectedPadW) {
            std::cerr << "[FAIL] " << label << ": pad flags mismatch\n";
            ok = false;
        }

        const cv::Mat reconstructed = haar_idwt2_level1(bands);
        if (!matsEqualExact(reconstructed, input)) {
            std::cerr << "[FAIL] " << label << ": reconstructed output mismatch\n";
            ok = false;
        }

        printMat(label + " LL", bands.LL);
        printMat(label + " LH", bands.LH);
        printMat(label + " HL", bands.HL);
        printMat(label + " HH", bands.HH);
        printMat(label + " Reconstructed", reconstructed);
        std::cout << label << " pad_h=" << bands.pad_h << " pad_w=" << bands.pad_w << "\n";

        if (ok) {
            std::cout << "[PASS] " << label << "\n";
        }

        return ok;
    }
} // namespace

int main() {
    bool ok = true;

    const cv::Mat input4x4 = makeSequentialMatrix(4, 4);
    const cv::Mat expectedLL4x4 = matFromRows({
        {2.5f, 4.5f},
        {10.5f, 12.5f},
    });
    const cv::Mat expectedLH4x4 = matFromRows({
        {-2.0f, -2.0f},
        {-2.0f, -2.0f},
    });
    const cv::Mat expectedHL4x4 = matFromRows({
        {-0.5f, -0.5f},
        {-0.5f, -0.5f},
    });
    const cv::Mat expectedHH4x4 = matFromRows({
        {0.0f, 0.0f},
        {0.0f, 0.0f},
    });

    ok &= verifyCase(
        "Roadscript Haar v1 4x4",
        input4x4,
        expectedLL4x4,
        expectedLH4x4,
        expectedHL4x4,
        expectedHH4x4,
        0,
        0
    );

    const cv::Mat input5x5 = makeSequentialMatrix(5, 5);
    const cv::Mat expectedLL5x5 = matFromRows({
        {3.0f, 5.0f, 6.5f},
        {13.0f, 15.0f, 16.5f},
        {20.5f, 22.5f, 24.0f},
    });
    const cv::Mat expectedLH5x5 = matFromRows({
        {-2.5f, -2.5f, -2.5f},
        {-2.5f, -2.5f, -2.5f},
        {0.0f, 0.0f, 0.0f},
    });
    const cv::Mat expectedHL5x5 = matFromRows({
        {-0.5f, -0.5f, 0.0f},
        {-0.5f, -0.5f, 0.0f},
        {-0.5f, -0.5f, 0.0f},
    });
    const cv::Mat expectedHH5x5 = matFromRows({
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},
    });

    ok &= verifyCase(
        "Roadscript Haar v1 5x5",
        input5x5,
        expectedLL5x5,
        expectedLH5x5,
        expectedHL5x5,
        expectedHH5x5,
        1,
        1
    );

    return ok ? 0 : 1;
}
