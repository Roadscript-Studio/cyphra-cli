#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {
    namespace fs = std::filesystem;

    bool containsAnsiEscape(const std::string &text) {
        return text.find("\x1b[") != std::string::npos;
    }

    std::string shellQuote(const fs::path &path) {
        const std::string raw = path.string();
        std::string quoted;
        quoted.reserve(raw.size() + 2);
        quoted.push_back('"');
        for (char ch: raw) {
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

    int runCommandCapture(
        const fs::path &binary,
        const std::string &arguments,
        const fs::path &outputFile
    ) {
        const std::string command =
            shellQuote(binary) + " " + arguments + " > " + shellQuote(outputFile) + " 2>&1";
        return std::system(command.c_str());
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

        const fs::path tempDir = makeUniqueTempDir("roadscript_cli_roundtrip");

        const fs::path inputPath = tempDir / "input.png";
        const fs::path outputPath = tempDir / "output.png";
        const fs::path embedLogPath = tempDir / "embed.log";
        const fs::path extractLogPath = tempDir / "extract.log";
        const fs::path verifyLogPath = tempDir / "verify.log";
        const fs::path helpLogPath = tempDir / "help.log";
        const fs::path configHelpLogPath = tempDir / "config-help.log";
        const fs::path configShowLogPath = tempDir / "config-show.log";
        const fs::path colorNeverLogPath = tempDir / "color-never.log";
        const fs::path colorAlwaysLogPath = tempDir / "color-always.log";
        const fs::path configColorNeverLogPath = tempDir / "config-color-never.log";
        const fs::path configColorAlwaysLogPath = tempDir / "config-color-always.log";

        cv::Mat input(256, 256, CV_8UC3, cv::Scalar(96, 128, 160));
        if (!cv::imwrite(inputPath.string(), input)) {
            std::cerr << "[FAIL] failed to save synthetic input image\n";
            return 1;
        }

        const int helpCode = runCommandCapture(
            rseBinary,
            "embed --help",
            helpLogPath
        );
        const std::string helpOutput = readTextFile(helpLogPath);
        if (helpCode != 0) {
            std::cerr << "[FAIL] embed --help exited with code " << helpCode << "\n";
            std::cerr << helpOutput;
            return 1;
        }
        if (helpOutput.find("USAGE") == std::string::npos ||
            helpOutput.find("REQUIRED OPTIONS") == std::string::npos ||
            helpOutput.find("EXAMPLES") == std::string::npos ||
            helpOutput.find("JSON OUTPUT") == std::string::npos) {
            std::cerr << "[FAIL] embed --help missing grouped help sections\n";
            std::cerr << helpOutput;
            return 1;
        }

        const int configHelpCode = runCommandCapture(
            rseBinary,
            "config --help",
            configHelpLogPath
        );
        const std::string configHelpOutput = readTextFile(configHelpLogPath);
        if (configHelpCode != 0) {
            std::cerr << "[FAIL] config --help exited with code " << configHelpCode << "\n";
            std::cerr << configHelpOutput;
            return 1;
        }
        if (configHelpOutput.find("Manage Configuration") == std::string::npos ||
            configHelpOutput.find("config show") == std::string::npos ||
            configHelpOutput.find("Persistent writable configuration commands may be added in a future release.") == std::string::npos) {
            std::cerr << "[FAIL] config --help missing configuration guidance\n";
            std::cerr << configHelpOutput;
            return 1;
        }

        const int configShowCode = runCommandCapture(
            rseBinary,
            "config show",
            configShowLogPath
        );
        const std::string configShowOutput = readTextFile(configShowLogPath);
        if (configShowCode != 0) {
            std::cerr << "[FAIL] config show exited with code " << configShowCode << "\n";
            std::cerr << configShowOutput;
            return 1;
        }
        if (configShowOutput.find("Roadscript Configuration") == std::string::npos ||
            configShowOutput.find("Profile") == std::string::npos ||
            configShowOutput.find("Defaults") == std::string::npos ||
            configShowOutput.find("Color") == std::string::npos) {
            std::cerr << "[FAIL] config show missing settings-page sections\n";
            std::cerr << configShowOutput;
            return 1;
        }

        const int embedCode = runCommandCapture(
            rseBinary,
            "embed --in " + shellQuote(inputPath) +
            " --out " + shellQuote(outputPath) +
            " --msg-block hello --protocol classic",
            embedLogPath
        );
        if (embedCode != 0) {
            std::cerr << "[FAIL] embed exited with code " << embedCode << "\n";
            std::cerr << readTextFile(embedLogPath);
            return 1;
        }

        const int extractCode = runCommandCapture(
            rseBinary,
            "extract --in " + shellQuote(outputPath) + " --protocol classic",
            extractLogPath
        );
        const std::string extractOutput = readTextFile(extractLogPath);
        if (extractCode != 0) {
            std::cerr << "[FAIL] extract exited with code " << extractCode << "\n";
            std::cerr << extractOutput;
            return 1;
        }
        if (extractOutput.find("hello") == std::string::npos) {
            std::cerr << "[FAIL] extract output did not contain decoded message\n";
            std::cerr << extractOutput;
            return 1;
        }

        const int verifyCode = runCommandCapture(
            rseBinary,
            "verify --in " + shellQuote(outputPath) + " --protocol classic",
            verifyLogPath
        );
        const std::string verifyOutput = readTextFile(verifyLogPath);
        if (verifyCode != 0) {
            std::cerr << "[FAIL] verify exited with code " << verifyCode << "\n";
            std::cerr << verifyOutput;
            return 1;
        }
        if (verifyOutput.find("Verification") == std::string::npos ||
            verifyOutput.find("PASS") == std::string::npos) {
            std::cerr << "[FAIL] verify output did not contain PASS\n";
            std::cerr << verifyOutput;
            return 1;
        }
        if (verifyOutput.find("Recovered payload") == std::string::npos) {
            std::cerr << "[FAIL] verify output did not contain recovered payload label\n";
            std::cerr << verifyOutput;
            return 1;
        }

        const int colorNeverCode = runCommandCapture(
            rseBinary,
            "verify --in " + shellQuote(outputPath) + " --protocol classic --color never",
            colorNeverLogPath
        );
        const std::string colorNeverOutput = readTextFile(colorNeverLogPath);
        if (colorNeverCode != 0) {
            std::cerr << "[FAIL] verify --color never exited with code " << colorNeverCode << "\n";
            std::cerr << colorNeverOutput;
            return 1;
        }
        if (containsAnsiEscape(colorNeverOutput)) {
            std::cerr << "[FAIL] verify --color never emitted ANSI escapes\n";
            std::cerr << colorNeverOutput;
            return 1;
        }

        const int colorAlwaysCode = runCommandCapture(
            rseBinary,
            "verify --in " + shellQuote(outputPath) + " --protocol classic --color always",
            colorAlwaysLogPath
        );
        const std::string colorAlwaysOutput = readTextFile(colorAlwaysLogPath);
        if (colorAlwaysCode != 0) {
            std::cerr << "[FAIL] verify --color always exited with code " << colorAlwaysCode << "\n";
            std::cerr << colorAlwaysOutput;
            return 1;
        }
        if (!containsAnsiEscape(colorAlwaysOutput)) {
            std::cerr << "[FAIL] verify --color always did not emit ANSI escapes\n";
            std::cerr << colorAlwaysOutput;
            return 1;
        }

        const int configColorNeverCode = runCommandCapture(
            rseBinary,
            "config show --color never",
            configColorNeverLogPath
        );
        const std::string configColorNeverOutput = readTextFile(configColorNeverLogPath);
        if (configColorNeverCode != 0) {
            std::cerr << "[FAIL] config show --color never exited with code " << configColorNeverCode << "\n";
            std::cerr << configColorNeverOutput;
            return 1;
        }
        if (containsAnsiEscape(configColorNeverOutput)) {
            std::cerr << "[FAIL] config show --color never emitted ANSI escapes\n";
            std::cerr << configColorNeverOutput;
            return 1;
        }

        const int configColorAlwaysCode = runCommandCapture(
            rseBinary,
            "config show --color always",
            configColorAlwaysLogPath
        );
        const std::string configColorAlwaysOutput = readTextFile(configColorAlwaysLogPath);
        if (configColorAlwaysCode != 0) {
            std::cerr << "[FAIL] config show --color always exited with code " << configColorAlwaysCode << "\n";
            std::cerr << configColorAlwaysOutput;
            return 1;
        }
        if (!containsAnsiEscape(configColorAlwaysOutput)) {
            std::cerr << "[FAIL] config show --color always did not emit ANSI escapes\n";
            std::cerr << configColorAlwaysOutput;
            return 1;
        }

        std::cout << "[PASS] cli_roundtrip_test\n";
        std::cout << extractOutput;
        std::cout << verifyOutput;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "[FAIL] exception: " << e.what() << "\n";
        return 1;
    }
}
