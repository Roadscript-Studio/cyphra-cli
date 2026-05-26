#include "roadscript/watermark/api.hpp"
#include "roadscript/watermark/prng.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <array>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {
    using roadscript::watermark::BGRAView;
    using roadscript::watermark::BGRAViewMut;
    using roadscript::watermark::PRNG;
    using roadscript::watermark::extractMessageBGRA;
    using roadscript::watermark::embedBGRA;

    cv::Mat makeSyntheticBGRA(int width, int height) {
        cv::Mat image(height, width, CV_8UC4);

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                auto &pixel = image.at<cv::Vec4b>(y, x);
                pixel[0] = static_cast<std::uint8_t>((x * 3 + y * 5) % 256);   // B
                pixel[1] = static_cast<std::uint8_t>((x * 7 + y * 11) % 256);  // G
                pixel[2] = static_cast<std::uint8_t>((x * 13 + y * 17) % 256); // R
                pixel[3] = static_cast<std::uint8_t>(64 + ((x + y) % 192));    // A
            }
        }

        return image;
    }

    BGRAView makeView(const cv::Mat &image) {
        return BGRAView{
            image.ptr<std::uint8_t>(),
            image.cols,
            image.rows,
            static_cast<int>(image.step)
        };
    }

    BGRAViewMut makeViewMut(cv::Mat &image) {
        return BGRAViewMut{
            image.ptr<std::uint8_t>(),
            image.cols,
            image.rows,
            static_cast<int>(image.step)
        };
    }

    bool runRoundTrip(
        const std::string &label,
        const std::string &message,
        const std::optional<std::string> &key,
        bool useKeyedSpatialShuffle
    ) {
        constexpr float kStep = 30.0f;
        cv::Mat source = makeSyntheticBGRA(256, 256);
        cv::Mat embedded(source.rows, source.cols, source.type());

        const bool embeddedOk = embedBGRA(
            makeView(source),
            makeViewMut(embedded),
            message,
            kStep,
            key,
            false,
            useKeyedSpatialShuffle
        );
        if (!embeddedOk) {
            std::cerr << "[FAIL] " << label << ": embedBGRA returned false\n";
            return false;
        }

        std::string recovered;
        const bool extractedOk = extractMessageBGRA(
            makeView(embedded),
            kStep,
            key,
            recovered,
            useKeyedSpatialShuffle
        );
        if (!extractedOk) {
            std::cerr << "[FAIL] " << label << ": extractMessageBGRA returned false\n";
            return false;
        }

        if (recovered != message) {
            std::cerr << "[FAIL] " << label << ": recovered message mismatch\n";
            std::cerr << "  expected: " << std::quoted(message) << "\n";
            std::cerr << "  actual:   " << std::quoted(recovered) << "\n";
            return false;
        }

        std::cout << "[PASS] " << label << "\n";
        return true;
    }
} // namespace

int main() {
    bool ok = true;
    constexpr std::array<std::uint32_t, 5> kExpectedBlocksU32 = {
        899501652u,
        4242122441u,
        3291822929u,
        3404865806u,
        1635478949u
    };
    const std::string kExpectedWhitenBits =
        "1101111111001110110101001111001011101111001000011001001100101001";

    std::cout << "Roadscript PRNG v1 golden vectors\n";

    {
        PRNG rng("test", "blocks");
        std::array<std::uint32_t, 5> actual{};
        std::cout << "key=test salt=blocks nextUint32[5]:";
        for (int i = 0; i < 5; ++i) {
            actual[static_cast<std::size_t>(i)] = rng.nextUint32();
            std::cout << (i == 0 ? " " : ", ") << actual[static_cast<std::size_t>(i)];
        }
        std::cout << "\n";

        if (actual != kExpectedBlocksU32) {
            std::cerr << "[FAIL] PRNG blocks golden vector mismatch\n";
            ok = false;
        }
    }

    {
        PRNG rng("test", "whiten");
        std::string bits;
        bits.reserve(64);

        std::uint32_t currentWord = 0;
        int bitsRemaining = 0;
        for (int i = 0; i < 64; ++i) {
            if (bitsRemaining == 0) {
                currentWord = rng.nextUint32();
                bitsRemaining = 32;
            }

            bits.push_back(((currentWord >> 31) & 1u) ? '1' : '0');
            currentWord <<= 1;
            bitsRemaining--;
        }

        std::cout << "key=test salt=whiten first64bits: " << bits << "\n";
        if (bits != kExpectedWhitenBits) {
            std::cerr << "[FAIL] PRNG whiten bit vector mismatch\n";
            ok = false;
        }
    }

    ok &= runRoundTrip(
        "BGRA unkeyed round-trip",
        "Roadscript unkeyed smoke",
        std::nullopt,
        false
    );

    ok &= runRoundTrip(
        "BGRA keyed round-trip",
        "Roadscript keyed smoke",
        std::optional<std::string>("test-key"),
        true
    );

    return ok ? 0 : 1;
}
