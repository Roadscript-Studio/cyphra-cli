#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
    namespace fs = std::filesystem;

    struct CommandResult {
        int exitCode = -1;
        std::string stdoutText;
        std::string stderrText;
    };

    std::string shellQuote(const fs::path &path) {
        const std::string raw = path.string();
        std::string quoted;
        quoted.reserve(raw.size() + 2);
        quoted.push_back('"');
        for (char ch : raw) {
            if (ch == '"' || ch == '\\') {
                quoted.push_back('\\');
            }
            quoted.push_back(ch);
        }
        quoted.push_back('"');
        return quoted;
    }

    std::string readTextFile(const fs::path &path) {
        std::ifstream in(path);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    CommandResult runCommandCapture(
        const std::string &command,
        const fs::path &stdoutFile,
        const fs::path &stderrFile
    ) {
        const std::string redirected =
            command + " > " + shellQuote(stdoutFile) + " 2> " + shellQuote(stderrFile);

        CommandResult result;
        result.exitCode = std::system(redirected.c_str());
        result.stdoutText = readTextFile(stdoutFile);
        result.stderrText = readTextFile(stderrFile);
        return result;
    }

    std::string trimWhitespace(const std::string &text) {
        std::size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
            start++;
        }
        std::size_t end = text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            end--;
        }
        return text.substr(start, end - start);
    }

    bool looksLikeJsonObject(const std::string &text) {
        const std::string trimmed = trimWhitespace(text);
        return trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}';
    }

    bool containsField(const std::string &jsonText, const std::string &fieldName) {
        return jsonText.find("\"" + fieldName + "\"") != std::string::npos;
    }

    std::size_t countOccurrences(const std::string &text, const std::string &needle) {
        if (needle.empty()) {
            return 0;
        }
        std::size_t count = 0;
        std::size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos) {
            ++count;
            pos += needle.size();
        }
        return count;
    }

    int require(bool condition, const std::string &message, const CommandResult &result) {
        if (condition) {
            return 0;
        }
        std::cerr << "[FAIL] " << message << "\n";
        std::cerr << "--- stdout ---\n" << result.stdoutText;
        std::cerr << "--- stderr ---\n" << result.stderrText;
        return 1;
    }

    fs::path makeUniqueTempDir(const char *prefix) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const fs::path tempDir = fs::temp_directory_path() / (std::string(prefix) + "-" + std::to_string(now));
        fs::create_directories(tempDir);
        return tempDir;
    }
}

int main(int argc, char **argv) {
    try {
        if (argc < 1 || argv[0] == nullptr) {
            std::cerr << "[FAIL] argv[0] unavailable\n";
            return 1;
        }

        const fs::path testBinary = fs::absolute(argv[0]);
        const fs::path buildDir = testBinary.parent_path();
        const fs::path rseBinary = buildDir / "rse";
        if (!fs::exists(rseBinary)) {
            std::cerr << "[FAIL] rse binary not found at: " << rseBinary << "\n";
            return 1;
        }

        const fs::path tempDir = makeUniqueTempDir("roadscript_mosaic_v2_cli");
        const fs::path inputPath = tempDir / "input.png";
        const fs::path outputPath = tempDir / "output.png";
        const fs::path debugJsonPath = tempDir / "mosaic-debug.json";
        const fs::path debugSvgPath = tempDir / "mosaic-debug.svg";
        const fs::path stdoutPath = tempDir / "stdout.log";
        const fs::path stderrPath = tempDir / "stderr.log";

        cv::Mat input(1136, 1136, CV_8UC3);
        for (int y = 0; y < input.rows; ++y) {
            auto *row = input.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input.cols; ++x) {
                row[x] = cv::Vec3b{
                    static_cast<unsigned char>((x * 17 + y * 3) & 0xff),
                    static_cast<unsigned char>((x * 5 + y * 11) & 0xff),
                    static_cast<unsigned char>((x * 7 + y * 13) & 0xff)
                };
            }
        }
        if (!cv::imwrite(inputPath.string(), input)) {
            std::cerr << "[FAIL] failed to save mosaic synthetic input image\n";
            return 1;
        }

        auto runWithArgs = [&](const std::string &args) {
            return runCommandCapture(
                shellQuote(rseBinary) + " " + args,
                stdoutPath,
                stderrPath
            );
        };

        const std::string message = "hello mosaic";

        CommandResult embedJson = runWithArgs(
            "embed --in " + shellQuote(inputPath) +
            " --out " + shellQuote(outputPath) +
            " --msg-block \"" + message + "\" --protocol mosaic --json"
        );
        if (int rc = require(embedJson.exitCode == 0, "mosaic embed exited nonzero", embedJson)) return rc;
        if (int rc = require(looksLikeJsonObject(embedJson.stdoutText), "mosaic embed stdout is not JSON", embedJson)) return rc;
        if (int rc = require(embedJson.stdoutText.find("\"protocol\": \"mosaic\"") != std::string::npos, "mosaic embed did not normalize protocol", embedJson)) return rc;
        if (int rc = require(containsField(embedJson.stdoutText, "valid_parity_fragments"), "mosaic embed missing valid_parity_fragments", embedJson)) return rc;

        CommandResult embedAliasJson = runWithArgs(
            "embed --in " + shellQuote(inputPath) +
            " --out " + shellQuote(outputPath) +
            " --msg-block \"" + message + "\" --protocol mosaic-v2 --json"
        );
        if (int rc = require(embedAliasJson.exitCode == 0, "mosaic-v2 alias embed exited nonzero", embedAliasJson)) return rc;
        if (int rc = require(embedAliasJson.stdoutText.find("\"protocol\": \"mosaic\"") != std::string::npos, "mosaic-v2 alias did not normalize protocol", embedAliasJson)) return rc;

        CommandResult extractHuman = runWithArgs(
            "extract --in " + shellQuote(outputPath) + " --protocol mosaic"
        );
        if (int rc = require(extractHuman.exitCode == 0, "mosaic extract exited nonzero", extractHuman)) return rc;
        if (int rc = require(extractHuman.stdoutText.find(message) != std::string::npos, "mosaic extract did not contain decoded message", extractHuman)) return rc;

        CommandResult verifyHuman = runWithArgs(
            "verify --in " + shellQuote(outputPath) + " --protocol mosaic"
        );
        if (int rc = require(verifyHuman.exitCode == 0, "mosaic verify exited nonzero", verifyHuman)) return rc;
        if (int rc = require(
                verifyHuman.stdoutText.find("Verification") != std::string::npos &&
                    verifyHuman.stdoutText.find("PASS") != std::string::npos,
                "mosaic verify did not PASS",
                verifyHuman
            )) return rc;

        CommandResult verifyJson = runWithArgs(
            "verify --in " + shellQuote(outputPath) +
            " --protocol mosaic --json --debug-json " + shellQuote(debugJsonPath) +
            " --debug-svg " + shellQuote(debugSvgPath)
        );
        if (int rc = require(verifyJson.exitCode == 0, "mosaic verify --json exited nonzero", verifyJson)) return rc;
        if (int rc = require(looksLikeJsonObject(verifyJson.stdoutText), "mosaic verify --json stdout is not JSON", verifyJson)) return rc;
        if (int rc = require(containsField(verifyJson.stdoutText, "metadata_recovered"), "mosaic verify --json missing metadata_recovered", verifyJson)) return rc;
        if (int rc = require(containsField(verifyJson.stdoutText, "recovered_via_parity"), "mosaic verify --json missing recovered_via_parity", verifyJson)) return rc;
        if (int rc = require(containsField(verifyJson.stdoutText, "local_search"), "mosaic verify --json missing local_search", verifyJson)) return rc;
        if (int rc = require(fs::exists(debugJsonPath), "mosaic debug JSON file was not created", verifyJson)) return rc;

        const std::string debugJson = readTextFile(debugJsonPath);
        if (int rc = require(looksLikeJsonObject(debugJson), "mosaic debug JSON file is not valid-looking JSON", verifyJson)) return rc;
        if (int rc = require(debugJson.find("\"protocol\": \"mosaic\"") != std::string::npos, "mosaic debug JSON did not record mosaic protocol", verifyJson)) return rc;
        if (int rc = require(containsField(debugJson, "cells"), "mosaic debug JSON missing cells array", verifyJson)) return rc;
        if (int rc = require(countOccurrences(debugJson, "\"packet_kind\"") == 36u, "mosaic debug JSON did not include 36 cell entries", verifyJson)) return rc;
        if (int rc = require(fs::exists(debugSvgPath), "mosaic debug SVG file was not created", verifyJson)) return rc;

        const std::string debugSvg = readTextFile(debugSvgPath);
        if (int rc = require(debugSvg.find("<svg") != std::string::npos, "mosaic debug SVG file does not look like SVG", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("Roadscript Mosaic Inspector") != std::string::npos, "mosaic debug SVG missing inspector title", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("Legend") != std::string::npos, "mosaic debug SVG missing Legend", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("Cell roles") != std::string::npos, "mosaic debug SVG missing Cell roles section", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("Recovery status") != std::string::npos, "mosaic debug SVG missing Recovery status section", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("Summary stats") != std::string::npos, "mosaic debug SVG missing Summary stats section", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("Locator") != std::string::npos, "mosaic debug SVG missing role legend labels", verifyJson)) return rc;
        if (int rc = require(debugSvg.find("metadata recovered") != std::string::npos, "mosaic debug SVG missing summary stats labels", verifyJson)) return rc;

        CommandResult negativeControl = runWithArgs(
            "verify --in " + shellQuote(inputPath) + " --protocol mosaic --json"
        );
        if (int rc = require(negativeControl.exitCode != 0, "mosaic negative control exited with code 0", negativeControl)) return rc;
        if (int rc = require(looksLikeJsonObject(negativeControl.stdoutText), "mosaic negative control stdout is not JSON", negativeControl)) return rc;
        if (int rc = require(containsField(negativeControl.stdoutText, "failure_reason"), "mosaic negative control missing failure_reason", negativeControl)) return rc;
        if (int rc = require(negativeControl.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "mosaic negative control did not report FAIL", negativeControl)) return rc;

        std::cout << "[PASS] mosaic_v2_cli_smoke\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "[FAIL] exception: " << e.what() << "\n";
        return 1;
    }
}
