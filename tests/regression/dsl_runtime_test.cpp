#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

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
        std::ifstream in(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    }

    void writeTextFile(const fs::path &path, const std::string &text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
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
} // namespace

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

        const fs::path tempDir = makeUniqueTempDir("roadscript_dsl_runtime");

        const fs::path inputPath = tempDir / "input.png";
        cv::Mat input(1136, 1136, CV_8UC3);
        for (int y = 0; y < input.rows; ++y) {
            auto *row = input.ptr<cv::Vec3b>(y);
            for (int x = 0; x < input.cols; ++x) {
                row[x] = cv::Vec3b{
                    static_cast<unsigned char>((x * 13 + y * 5) & 0xff),
                    static_cast<unsigned char>((x * 7 + y * 17) & 0xff),
                    static_cast<unsigned char>((x * 3 + y * 19) & 0xff)
                };
            }
        }
        if (!cv::imwrite(inputPath.string(), input)) {
            std::cerr << "[FAIL] failed to save synthetic input image\n";
            return 1;
        }

        const fs::path stdoutPath = tempDir / "stdout.log";
        const fs::path stderrPath = tempDir / "stderr.log";

        auto runScript = [&](const fs::path &scriptPath, const std::string &extraArgs = std::string()) {
            const std::string command =
                shellQuote(rseBinary) + " run " + shellQuote(scriptPath) +
                (extraArgs.empty() ? "" : " " + extraArgs);
            return runCommandCapture(command, stdoutPath, stderrPath);
        };

        {
            const fs::path outputPath = tempDir / "dry-run-classic.png";
            const fs::path scriptPath = tempDir / "dry-run.rsx";
            const std::string secretKey = "top-secret-dry-run-key";
            writeTextFile(
                scriptPath,
                "embed {\n"
                "  in: \"" + inputPath.string() + "\"\n"
                "  out: \"" + outputPath.string() + "\"\n"
                "  msg_block: \"hello dry run\"\n"
                "  key: \"" + secretKey + "\"\n"
                "  protocol: classic\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath, "--dry-run");
            if (int rc = require(result.exitCode == 0, "dry-run workflow exited nonzero", result)) return rc;
            if (int rc = require(result.stdoutText.find("Workflow Summary") != std::string::npos, "dry-run summary missing", result)) return rc;
            if (int rc = require(result.stdoutText.find("Dry run") != std::string::npos, "dry-run header missing", result)) return rc;
            if (int rc = require(result.stdoutText.find(secretKey) == std::string::npos, "dry-run leaked key contents", result)) return rc;
            if (int rc = require(!fs::exists(outputPath), "dry-run created output unexpectedly", result)) return rc;
        }

        {
            const fs::path outputPath = tempDir / "classic.png";
            const fs::path scriptPath = tempDir / "classic.rsx";
            writeTextFile(
                scriptPath,
                "let input = \"" + inputPath.string() + "\"\n"
                "let output = \"" + outputPath.string() + "\"\n"
                "embed {\n"
                "  in: input\n"
                "  out: output\n"
                "  msg_block: \"hello classic workflow\"\n"
                "  protocol: classic\n"
                "}\n"
                "extract {\n"
                "  in: output\n"
                "  protocol: classic\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode == 0, "classic workflow exited nonzero", result)) return rc;
            if (int rc = require(result.stdoutText.find("hello classic workflow") != std::string::npos, "classic extract output missing decoded message", result)) return rc;
            if (int rc = require(fs::exists(outputPath), "classic workflow did not create output image", result)) return rc;
        }

        {
            const fs::path outputPath = tempDir / "mosaic.png";
            const fs::path debugJsonPath = tempDir / "mosaic-debug.json";
            const fs::path debugSvgPath = tempDir / "mosaic-debug.svg";
            const fs::path scriptPath = tempDir / "mosaic.rsx";
            writeTextFile(
                scriptPath,
                "let input = \"" + inputPath.string() + "\"\n"
                "let output = \"" + outputPath.string() + "\"\n"
                "embed {\n"
                "  in: input\n"
                "  out: output\n"
                "  msg_block: \"hello mosaic\"\n"
                "  protocol: mosaic\n"
                "}\n"
                "verify {\n"
                "  in: output\n"
                "  protocol: mosaic\n"
                "  debug_json: \"" + debugJsonPath.string() + "\"\n"
                "  debug_svg: \"" + debugSvgPath.string() + "\"\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode == 0, "mosaic workflow exited nonzero", result)) return rc;
            if (int rc = require(
                    result.stdoutText.find("Verification") != std::string::npos &&
                        result.stdoutText.find("PASS") != std::string::npos,
                    "mosaic workflow verify did not PASS",
                    result
                )) return rc;
            if (int rc = require(fs::exists(debugJsonPath), "mosaic workflow did not create debug JSON", result)) return rc;
            if (int rc = require(fs::exists(debugSvgPath), "mosaic workflow did not create debug SVG", result)) return rc;
        }

        {
            const fs::path markerPath = tempDir / "marker.txt";
            const fs::path scriptPath = tempDir / "if-exists.rsx";
            writeTextFile(markerPath, "marker");
            writeTextFile(
                scriptPath,
                "if exists(\"" + markerPath.string() + "\") {\n"
                "  version {}\n"
                "} else {\n"
                "  doctor {}\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode == 0, "if exists workflow exited nonzero", result)) return rc;
            if (int rc = require(result.stdoutText.find("Roadscript Engine") != std::string::npos, "if exists branch did not run version", result)) return rc;
        }

        {
            const fs::path missingPath = tempDir / "missing.txt";
            const fs::path scriptPath = tempDir / "if-not-exists.rsx";
            writeTextFile(
                scriptPath,
                "if not exists(\"" + missingPath.string() + "\") {\n"
                "  doctor {}\n"
                "} else {\n"
                "  version {}\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode == 0, "if not exists workflow exited nonzero", result)) return rc;
            if (int rc = require(result.stdoutText.find("Roadscript Doctor") != std::string::npos, "if not exists branch did not run doctor", result)) return rc;
        }

        {
            const fs::path globDir = tempDir / "glob";
            fs::create_directories(globDir);
            cv::Mat a(64, 64, CV_8UC3, cv::Scalar(50, 80, 110));
            cv::Mat b(64, 64, CV_8UC3, cv::Scalar(10, 40, 90));
            cv::imwrite((globDir / "a.png").string(), a);
            cv::imwrite((globDir / "b.png").string(), b);
            const fs::path scriptPath = tempDir / "glob.rsx";
            writeTextFile(
                scriptPath,
                "for file in glob(\"" + (globDir / "*.png").string() + "\") {\n"
                "  info {\n"
                "    in: file\n"
                "    protocol: classic\n"
                "  }\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode == 0, "glob workflow exited nonzero", result)) return rc;
            if (int rc = require(result.stdoutText.find("Info") != std::string::npos, "glob workflow did not print info output", result)) return rc;
        }

        {
            const fs::path scriptPath = tempDir / "empty-glob.rsx";
            writeTextFile(
                scriptPath,
                "for file in glob(\"" + (tempDir / "no-matches" / "*.png").string() + "\") {\n"
                "  info {\n"
                "    in: file\n"
                "    protocol: classic\n"
                "  }\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode == 0, "empty glob workflow exited nonzero", result)) return rc;
            if (int rc = require(result.stdoutText.find("0 matches; loop skipped") != std::string::npos, "empty glob workflow did not report skipped loop", result)) return rc;
        }

        {
            const fs::path scriptPath = tempDir / "stop-on-failure.rsx";
            writeTextFile(
                scriptPath,
                "embed {\n"
                "  in: \"" + (tempDir / "missing.png").string() + "\"\n"
                "  out: \"" + (tempDir / "never-created.png").string() + "\"\n"
                "  msg_block: \"hello\"\n"
                "  protocol: classic\n"
                "}\n"
                "doctor {}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode != 0, "failed workflow unexpectedly succeeded", result)) return rc;
            if (int rc = require(result.stderrText.find("stop-on-failure.rsx:1:1") != std::string::npos, "failed workflow missing source span", result)) return rc;
            if (int rc = require(result.stderrText.find("step 1 (embed) failed") != std::string::npos, "failed workflow missing step failure diagnostic", result)) return rc;
            if (int rc = require(result.stdoutText.find("Roadscript Doctor") == std::string::npos, "workflow did not stop after failure", result)) return rc;
        }

        {
            const fs::path scriptPath = tempDir / "invalid.rsx";
            writeTextFile(
                scriptPath,
                "info {\n"
                "  in: missing_input\n"
                "  protocol: classic\n"
                "}\n"
            );
            const CommandResult result = runScript(scriptPath);
            if (int rc = require(result.exitCode != 0, "invalid workflow unexpectedly succeeded", result)) return rc;
            if (int rc = require(result.stderrText.find("invalid.rsx:2:") != std::string::npos, "invalid workflow missing source diagnostic", result)) return rc;
        }

        {
            const fs::path repoRoot = buildDir.parent_path();
            const fs::path classicExample = repoRoot / "examples" / "classic_roundtrip.rsx";
            const fs::path mosaicExample = repoRoot / "examples" / "mosaic_debug.rsx";
            const fs::path batchExample = repoRoot / "examples" / "batch_info.rsx";

            const CommandResult classicResult = runScript(classicExample, "--dry-run");
            if (int rc = require(classicResult.exitCode == 0, "classic example dry-run failed", classicResult)) return rc;

            const CommandResult mosaicResult = runScript(mosaicExample, "--dry-run");
            if (int rc = require(mosaicResult.exitCode == 0, "mosaic example dry-run failed", mosaicResult)) return rc;

            const CommandResult batchResult = runScript(batchExample, "--dry-run");
            if (int rc = require(batchResult.exitCode == 0, "batch example dry-run failed", batchResult)) return rc;
        }

        std::cout << "[PASS] dsl_runtime_test\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "[FAIL] exception: " << e.what() << "\n";
        return 1;
    }
}
