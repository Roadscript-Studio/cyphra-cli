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
        size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
            start++;
        }

        size_t end = text.size();
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

    bool containsAnsiEscape(const std::string &text) {
        return text.find("\x1b[") != std::string::npos;
    }

    bool containsHumanPrefixBeforeJson(const std::string &text) {
        const std::string trimmed = trimWhitespace(text);
        return trimmed.empty() || trimmed.front() != '{';
    }

    int require(
        bool condition,
        const std::string &message,
        const CommandResult &result
    ) {
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

        const fs::path tempDir = makeUniqueTempDir("roadscript_cli_json");

        const fs::path inputPath = tempDir / "input.png";
        const fs::path outputPath = tempDir / "output.png";
        const fs::path stdoutPath = tempDir / "stdout.log";
        const fs::path stderrPath = tempDir / "stderr.log";

        cv::Mat input(256, 256, CV_8UC3, cv::Scalar(96, 128, 160));
        if (!cv::imwrite(inputPath.string(), input)) {
            std::cerr << "[FAIL] failed to save synthetic input image\n";
            return 1;
        }

        auto runWithArgs = [&](const std::string &args) {
            return runCommandCapture(
                shellQuote(rseBinary) + " " + args,
                stdoutPath,
                stderrPath
            );
        };

        CommandResult infoResult = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --json"
        );
        if (int rc = require(infoResult.exitCode == 0, "info --json exited nonzero", infoResult)) return rc;
        if (int rc = require(looksLikeJsonObject(infoResult.stdoutText), "info --json stdout is not a JSON object", infoResult)) return rc;
        if (int rc = require(!containsHumanPrefixBeforeJson(infoResult.stdoutText), "info --json contains human-readable text before JSON", infoResult)) return rc;
        if (int rc = require(containsField(infoResult.stdoutText, "status"), "info --json missing status field", infoResult)) return rc;
        if (int rc = require(containsField(infoResult.stdoutText, "capacity_bits"), "info --json missing capacity_bits field", infoResult)) return rc;
        if (int rc = require(containsField(infoResult.stdoutText, "protocol"), "info --json missing protocol field", infoResult)) return rc;
        if (int rc = require(infoResult.stdoutText.find("\"protocol\": \"classic\"") != std::string::npos, "info --json did not normalize classic protocol name", infoResult)) return rc;

        CommandResult embedResult = runWithArgs(
            "embed --in " + shellQuote(inputPath) +
            " --out " + shellQuote(outputPath) +
            " --msg-block hello --json --protocol classic"
        );
        if (int rc = require(embedResult.exitCode == 0, "embed --json exited nonzero", embedResult)) return rc;
        if (int rc = require(looksLikeJsonObject(embedResult.stdoutText), "embed --json stdout is not a JSON object", embedResult)) return rc;
        if (int rc = require(!containsHumanPrefixBeforeJson(embedResult.stdoutText), "embed --json contains human-readable text before JSON", embedResult)) return rc;
        if (int rc = require(containsField(embedResult.stdoutText, "status"), "embed --json missing status field", embedResult)) return rc;
        if (int rc = require(containsField(embedResult.stdoutText, "embed_time_ms"), "embed --json missing embed_time_ms field", embedResult)) return rc;
        if (int rc = require(containsField(embedResult.stdoutText, "protocol"), "embed --json missing protocol field", embedResult)) return rc;

        CommandResult verifyResult = runWithArgs(
            "verify --in " + shellQuote(outputPath) + " --json --step-search --protocol classic --color always"
        );
        if (int rc = require(verifyResult.exitCode == 0, "verify --json exited nonzero", verifyResult)) return rc;
        if (int rc = require(looksLikeJsonObject(verifyResult.stdoutText), "verify --json stdout is not a JSON object", verifyResult)) return rc;
        if (int rc = require(!containsHumanPrefixBeforeJson(verifyResult.stdoutText), "verify --json contains human-readable text before JSON", verifyResult)) return rc;
        if (int rc = require(!containsAnsiEscape(verifyResult.stdoutText), "verify --json emitted ANSI escapes", verifyResult)) return rc;
        if (int rc = require(containsField(verifyResult.stdoutText, "status"), "verify --json missing status field", verifyResult)) return rc;
        if (int rc = require(containsField(verifyResult.stdoutText, "verify_time_ms"), "verify --json missing verify_time_ms field", verifyResult)) return rc;
        if (int rc = require(containsField(verifyResult.stdoutText, "decoded_message"), "verify --json missing decoded_message field", verifyResult)) return rc;
        if (int rc = require(containsField(verifyResult.stdoutText, "step_search"), "verify --json missing step_search field", verifyResult)) return rc;
        if (int rc = require(containsField(verifyResult.stdoutText, "step_used"), "verify --json missing step_used field", verifyResult)) return rc;
        if (int rc = require(containsField(verifyResult.stdoutText, "protocol"), "verify --json missing protocol field", verifyResult)) return rc;

        CommandResult unknownProtocolFailure = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --protocol nope --json"
        );
        if (int rc = require(unknownProtocolFailure.exitCode != 0, "unknown protocol failure exited with code 0", unknownProtocolFailure)) return rc;
        if (int rc = require(looksLikeJsonObject(unknownProtocolFailure.stdoutText), "unknown protocol stdout is not JSON", unknownProtocolFailure)) return rc;
        if (int rc = require(containsField(unknownProtocolFailure.stdoutText, "error"), "unknown protocol failure missing error field", unknownProtocolFailure)) return rc;
        if (int rc = require(unknownProtocolFailure.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "unknown protocol failure did not report FAIL", unknownProtocolFailure)) return rc;

        CommandResult mosaicInfoResult = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --protocol mosaic --json"
        );
        if (int rc = require(mosaicInfoResult.exitCode == 0, "mosaic info exited nonzero", mosaicInfoResult)) return rc;
        if (int rc = require(looksLikeJsonObject(mosaicInfoResult.stdoutText), "mosaic info stdout is not JSON", mosaicInfoResult)) return rc;
        if (int rc = require(mosaicInfoResult.stdoutText.find("\"protocol\": \"mosaic\"") != std::string::npos, "mosaic info did not normalize protocol name", mosaicInfoResult)) return rc;
        if (int rc = require(mosaicInfoResult.stdoutText.find("\"supported\": false") != std::string::npos, "mosaic info should report unsupported for small input", mosaicInfoResult)) return rc;
        if (int rc = require(mosaicInfoResult.stdoutText.find("minimum supported dimension") != std::string::npos, "mosaic info should report support threshold reason", mosaicInfoResult)) return rc;
        if (int rc = require(containsField(mosaicInfoResult.stdoutText, "overhead_bits"), "mosaic info missing overhead_bits field", mosaicInfoResult)) return rc;
        if (int rc = require(containsField(mosaicInfoResult.stdoutText, "effective_payload_bits"), "mosaic info missing effective_payload_bits field", mosaicInfoResult)) return rc;
        if (int rc = require(containsField(mosaicInfoResult.stdoutText, "locator_cells"), "mosaic info missing locator_cells field", mosaicInfoResult)) return rc;
        if (int rc = require(containsField(mosaicInfoResult.stdoutText, "timing_cells"), "mosaic info missing timing_cells field", mosaicInfoResult)) return rc;
        if (int rc = require(containsField(mosaicInfoResult.stdoutText, "metadata_cells"), "mosaic info missing metadata_cells field", mosaicInfoResult)) return rc;
        if (int rc = require(containsField(mosaicInfoResult.stdoutText, "data_cells"), "mosaic info missing data_cells field", mosaicInfoResult)) return rc;

        CommandResult mosaicEmbedTooSmall = runWithArgs(
            "embed --in " + shellQuote(inputPath) +
            " --out " + shellQuote(outputPath) +
            " --msg-block hello --json --protocol mosaic"
        );
        if (int rc = require(mosaicEmbedTooSmall.exitCode != 0, "mosaic embed on small input should fail", mosaicEmbedTooSmall)) return rc;
        if (int rc = require(looksLikeJsonObject(mosaicEmbedTooSmall.stdoutText), "mosaic embed too-small stdout is not JSON", mosaicEmbedTooSmall)) return rc;
        if (int rc = require(containsField(mosaicEmbedTooSmall.stdoutText, "error"), "mosaic embed too-small missing error field", mosaicEmbedTooSmall)) return rc;
        if (int rc = require(mosaicEmbedTooSmall.stdoutText.find("minimum supported dimension is 1136 px") != std::string::npos, "mosaic embed too-small did not report preflight threshold", mosaicEmbedTooSmall)) return rc;
        if (int rc = require(mosaicEmbedTooSmall.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "mosaic embed too-small did not report FAIL", mosaicEmbedTooSmall)) return rc;

        CommandResult classicAliasInfoResult = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --protocol v1 --json"
        );
        if (int rc = require(classicAliasInfoResult.exitCode == 0, "v1 alias info exited nonzero", classicAliasInfoResult)) return rc;
        if (int rc = require(classicAliasInfoResult.stdoutText.find("\"protocol\": \"classic\"") != std::string::npos, "v1 alias did not normalize to classic", classicAliasInfoResult)) return rc;

        CommandResult mosaicAliasInfoResult = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --protocol mosaic-v2 --json"
        );
        if (int rc = require(mosaicAliasInfoResult.exitCode == 0, "mosaic-v2 alias info exited nonzero", mosaicAliasInfoResult)) return rc;
        if (int rc = require(mosaicAliasInfoResult.stdoutText.find("\"protocol\": \"mosaic\"") != std::string::npos, "mosaic-v2 alias did not normalize to mosaic", mosaicAliasInfoResult)) return rc;

        CommandResult maxMessageFailure = runWithArgs(
            "embed --in " + shellQuote(inputPath) +
            " --out " + shellQuote(outputPath) +
            " --msg-block hello --max-message-bytes 3 --json"
        );
        if (int rc = require(maxMessageFailure.exitCode != 0, "message guardrail failure exited with code 0", maxMessageFailure)) return rc;
        if (int rc = require(looksLikeJsonObject(maxMessageFailure.stdoutText), "message guardrail failure stdout is not JSON", maxMessageFailure)) return rc;
        if (int rc = require(containsField(maxMessageFailure.stdoutText, "status"), "message guardrail failure missing status field", maxMessageFailure)) return rc;
        if (int rc = require(containsField(maxMessageFailure.stdoutText, "error"), "message guardrail failure missing error field", maxMessageFailure)) return rc;
        if (int rc = require(maxMessageFailure.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "message guardrail failure did not report FAIL", maxMessageFailure)) return rc;

        CommandResult maxPixelsFailure = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --max-pixels 1000 --json"
        );
        if (int rc = require(maxPixelsFailure.exitCode != 0, "pixel guardrail failure exited with code 0", maxPixelsFailure)) return rc;
        if (int rc = require(looksLikeJsonObject(maxPixelsFailure.stdoutText), "pixel guardrail failure stdout is not JSON", maxPixelsFailure)) return rc;
        if (int rc = require(containsField(maxPixelsFailure.stdoutText, "error"), "pixel guardrail failure missing error field", maxPixelsFailure)) return rc;
        if (int rc = require(maxPixelsFailure.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "pixel guardrail failure did not report FAIL", maxPixelsFailure)) return rc;

        CommandResult invalidStepFailure = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --step inf --json"
        );
        if (int rc = require(invalidStepFailure.exitCode != 0, "non-finite --step exited with code 0", invalidStepFailure)) return rc;
        if (int rc = require(looksLikeJsonObject(invalidStepFailure.stdoutText), "non-finite --step stdout is not JSON", invalidStepFailure)) return rc;
        if (int rc = require(containsField(invalidStepFailure.stdoutText, "error"), "non-finite --step missing error field", invalidStepFailure)) return rc;
        if (int rc = require(invalidStepFailure.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "non-finite --step did not report FAIL", invalidStepFailure)) return rc;

        CommandResult conflictingKeysFailure = runCommandCapture(
            "env RSE_KEY=queens " + shellQuote(rseBinary) + " info --in " + shellQuote(inputPath) + " --key a --key-env RSE_KEY --json",
            stdoutPath,
            stderrPath
        );
        if (int rc = require(conflictingKeysFailure.exitCode != 0, "conflicting key sources failure exited with code 0", conflictingKeysFailure)) return rc;
        if (int rc = require(looksLikeJsonObject(conflictingKeysFailure.stdoutText), "conflicting key sources stdout is not JSON", conflictingKeysFailure)) return rc;
        if (int rc = require(containsField(conflictingKeysFailure.stdoutText, "error"), "conflicting key sources failure missing error field", conflictingKeysFailure)) return rc;
        if (int rc = require(conflictingKeysFailure.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "conflicting key sources failure did not report FAIL", conflictingKeysFailure)) return rc;

        CommandResult missingEnvFailure = runWithArgs(
            "info --in " + shellQuote(inputPath) + " --key-env RSE_KEY_MISSING_FOR_TEST --json"
        );
        if (int rc = require(missingEnvFailure.exitCode != 0, "missing --key-env variable exited with code 0", missingEnvFailure)) return rc;
        if (int rc = require(looksLikeJsonObject(missingEnvFailure.stdoutText), "missing --key-env stdout is not JSON", missingEnvFailure)) return rc;
        if (int rc = require(containsField(missingEnvFailure.stdoutText, "error"), "missing --key-env failure missing error field", missingEnvFailure)) return rc;
        if (int rc = require(missingEnvFailure.stdoutText.find("\"status\": \"FAIL\"") != std::string::npos, "missing --key-env failure did not report FAIL", missingEnvFailure)) return rc;

        std::cout << "[PASS] cli_json_test\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "[FAIL] exception: " << e.what() << "\n";
        return 1;
    }
}
