#include "cli_commands.hpp"

#include "cli_debug_artifacts.hpp"
#include "cli_dsl_runtime.hpp"
#include "cli_ir_validation.hpp"
#include "cli_parse.hpp"
#include "cli_print.hpp"

#include "roadscript/watermark/api.hpp"
#include "roadscript/watermark/core.hpp"
#include "roadscript/watermark/extract.hpp"
#include "roadscript/watermark/mosaic_info.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace roadscript::cli {
    namespace {
        std::optional<ColorMode> prescanColorMode(int argc, char **argv) {
            std::optional<ColorMode> parsed;
            for (int i = 1; i < argc; ++i) {
                if (argv[i] == nullptr || std::string_view(argv[i]) != "--color") {
                    continue;
                }
                if (i + 1 >= argc || argv[i + 1] == nullptr) {
                    return std::nullopt;
                }
                const std::string value = argv[++i];
                if (value == "auto") {
                    parsed = ColorMode::Auto;
                } else if (value == "always") {
                    parsed = ColorMode::Always;
                } else if (value == "never") {
                    parsed = ColorMode::Never;
                } else {
                    return std::nullopt;
                }
            }
            return parsed;
        }

        namespace fs = std::filesystem;

        cv::Mat loadImageUnchanged(const std::string &path) {
            if (!fs::exists(path) || !fs::is_regular_file(path)) {
                return {};
            }
            return cv::imread(path, cv::IMREAD_UNCHANGED);
        }

        cv::Mat ensureBGRA(const cv::Mat &image) {
            if (image.empty()) {
                throw std::runtime_error("image is empty");
            }

            if (image.type() == CV_8UC4) {
                return image;
            } else if (image.type() == CV_8UC3) {
                cv::Mat bgra;
                cv::cvtColor(image, bgra, cv::COLOR_BGR2BGRA);
                return bgra;
            } else if (image.type() == CV_8UC1) {
                cv::Mat bgra;
                cv::cvtColor(image, bgra, cv::COLOR_GRAY2BGRA);
                return bgra;
            } else {
                throw std::runtime_error("unsupported image format");
            }
        }

        cv::Mat ensureBGR(const cv::Mat &image) {
            if (image.empty()) {
                throw std::runtime_error("image is empty");
            }

            if (image.type() == CV_8UC3) {
                return image;
            } else if (image.type() == CV_8UC4) {
                cv::Mat bgr;
                cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
                return bgr;
            } else if (image.type() == CV_8UC1) {
                cv::Mat bgr;
                cv::cvtColor(image, bgr, cv::COLOR_GRAY2BGR);
                return bgr;
            } else {
                throw std::runtime_error("unsupported image format");
            }
        }

        roadscript::watermark::BGRAView makeView(const cv::Mat &image) {
            return roadscript::watermark::BGRAView{
                image.ptr<std::uint8_t>(),
                image.cols,
                image.rows,
                static_cast<int>(image.step)
            };
        }

        roadscript::watermark::BGRAViewMut makeViewMut(cv::Mat &image) {
            return roadscript::watermark::BGRAViewMut{
                image.ptr<std::uint8_t>(),
                image.cols,
                image.rows,
                static_cast<int>(image.step)
            };
        }

        int reportCommandError(bool jsonMode, const std::string &message) {
            if (jsonMode) {
                printJsonError(std::cout, message);
            } else {
                printHumanError(std::cerr, message);
            }
            return 1;
        }

        int cmdInfo(CommandIR &command, std::string_view argv0) {
            CommonOptions &options = command.options;
            if (resolveCommandKeySource(command) != 0) {
                return 1;
            }
            if (validateCommandRequirements(command, argv0) != 0) {
                return 1;
            }

            cv::Mat image = loadImageUnchanged(options.inputPath);
            if (image.empty()) {
                if (options.json) {
                    printJsonError(std::cout, "failed to load input image: " + options.inputPath);
                } else {
                    printHumanError(std::cerr, "failed to load input image: " + options.inputPath);
                    printTryLine(std::cerr, std::string(argv0) + " info --in input.jpg");
                }
                return 1;
            }
            if (checkImageGuardrails(image, command) != 0) {
                return 1;
            }

            if (roadscript::watermark::isMosaicProtocol(options.protocol)) {
                const auto plan =
                    roadscript::watermark::planMosaicInfoExperimental(image.cols, image.rows);
                const bool fits = !options.hasMessage ||
                    static_cast<unsigned long long>(options.message.size()) <=
                        static_cast<unsigned long long>(plan.effectivePayloadBytes);
                if (options.hasMessage && checkMessageGuardrail(options.message, command) != 0) {
                    return 1;
                }

                if (options.json) {
                    bool first = true;
                    std::cout << "{\n";
                    jsonFieldString(std::cout, first, "status", "OK");
                    jsonFieldString(std::cout, first, "input", options.inputPath);
                    jsonFieldString(std::cout, first, "protocol", protocolToString(options.protocol));
                    jsonFieldInt(std::cout, first, "width", image.cols);
                    jsonFieldInt(std::cout, first, "height", image.rows);
                    jsonFieldDouble(std::cout, first, "step", options.step, 2);
                    jsonFieldString(std::cout, first, "layout", "mosaic-6x6-experimental");
                    jsonFieldString(
                        std::cout,
                        first,
                        "key_mode",
                        options.key.has_value() ? "provided" : "none"
                    );
                    jsonFieldBool(std::cout, first, "experimental", true);
                    jsonFieldBool(std::cout, first, "embed_implemented", false);
                    jsonFieldBool(std::cout, first, "decode_implemented", false);
                    jsonFieldBool(std::cout, first, "supported", plan.supported);
                    if (!plan.reason.empty()) {
                        jsonFieldString(std::cout, first, "reason", plan.reason);
                    }
                    jsonFieldInt(std::cout, first, "raw_usable_capacity_bits", plan.rawUsableCapacityBits);
                    jsonFieldInt(std::cout, first, "overhead_bits", plan.overheadBits);
                    jsonFieldInt(std::cout, first, "effective_payload_bits", plan.effectivePayloadBits);
                    jsonFieldInt(std::cout, first, "effective_payload_bytes", plan.effectivePayloadBytes);
                    jsonFieldInt(std::cout, first, "locator_cells", plan.locatorCellCount);
                    jsonFieldInt(std::cout, first, "timing_cells", plan.timingCellCount);
                    jsonFieldInt(std::cout, first, "metadata_cells", plan.metadataCellCount);
                    jsonFieldInt(std::cout, first, "metadata_copies", plan.metadataCopies);
                    jsonFieldInt(std::cout, first, "data_cells", plan.dataCellCount);
                    jsonFieldInt(std::cout, first, "fragment_count", plan.fragmentCount);
                    jsonFieldInt(std::cout, first, "source_fragment_count", plan.sourceFragmentCount);
                    jsonFieldInt(std::cout, first, "parity_count", plan.parityCount);
                    jsonFieldBool(
                        std::cout,
                        first,
                        "local_search_planned_default",
                        plan.localSearchPlannedDefault
                    );
                    if (options.hasMessage) {
                        jsonFieldInt(
                            std::cout,
                            first,
                            "message_bytes",
                            static_cast<int>(options.message.size())
                        );
                        jsonFieldBool(std::cout, first, "fits", fits);
                    }
                    std::cout << "\n}\n";
                    return 0;
                }

                printSectionHeader(std::cout, "Mosaic Info");
                printKeyValue(std::cout, "Input", options.inputPath);
                printKeyValue(std::cout, "Protocol", formatProtocolLabel(options.protocol));
                printKeyValue(std::cout, "Status", "experimental planning only");
                printKeyValue(std::cout, "Embed/decode", "not implemented yet");
                printKeyValue(std::cout, "Supported", formatBoolYesNo(plan.supported));
                printKeyValue(
                    std::cout,
                    "Support envelope",
                    plan.supported ? "inside current conservative Mosaic minimum" : "outside current conservative Mosaic minimum"
                );
                printKeyValue(
                    std::cout,
                    "Minimum dimension",
                    roadscript::watermark::MOSAIC_V2_MIN_SUPPORTED_DIMENSION
                );
                if (!plan.reason.empty()) {
                    printKeyValue(std::cout, "Reason", plan.reason);
                }
                printKeyValue(std::cout, "Width", image.cols);
                printKeyValue(std::cout, "Height", image.rows);
                printKeyValue(std::cout, "Layout", "mosaic-6x6-experimental");
                printKeyValue(std::cout, "Raw capacity bits", plan.rawUsableCapacityBits);
                printKeyValue(std::cout, "Overhead bits", plan.overheadBits);
                printKeyValue(std::cout, "Payload bits", plan.effectivePayloadBits);
                printKeyValue(std::cout, "Payload bytes", plan.effectivePayloadBytes);
                printKeyValue(std::cout, "Locator cells", plan.locatorCellCount);
                printKeyValue(std::cout, "Timing cells", plan.timingCellCount);
                printKeyValue(std::cout, "Metadata cells", plan.metadataCellCount);
                printKeyValue(std::cout, "Metadata copies", plan.metadataCopies);
                printKeyValue(std::cout, "Data cells", plan.dataCellCount);
                printKeyValue(std::cout, "Fragment count", plan.fragmentCount);
                printKeyValue(std::cout, "Source fragments", plan.sourceFragmentCount);
                printKeyValue(std::cout, "Parity count", plan.parityCount);
                printKeyValue(std::cout, "Local search", formatBoolOnOff(plan.localSearchPlannedDefault));
                if (options.hasMessage) {
                    printKeyValue(std::cout, "Message bytes", options.message.size());
                    printKeyValue(std::cout, "Fits", formatBoolYesNo(fits));
                }
                return 0;
            }

            const int capacityBits = roadscript::watermark::capacityBitsForBGRA(
                image.cols,
                image.rows,
                usesKeyedShuffle(options.layout)
            );
            const int capacityBytes = capacityBits / 8;

            if (options.hasMessage) {
                if (checkMessageGuardrail(options.message, command) != 0) {
                    return 1;
                }
                const auto plan = roadscript::watermark::planClassicMessage(
                    options.message,
                    options.key.has_value()
                );
                const std::size_t messageBytes = options.message.size();
                const int requiredBits = plan.requiredBits;
                const double utilization = capacityBits > 0
                    ? (100.0 * static_cast<double>(requiredBits) / static_cast<double>(capacityBits))
                    : 0.0;

                if (options.json) {
                    bool first = true;
                    std::cout << "{\n";
                    jsonFieldString(std::cout, first, "status", "OK");
                    jsonFieldString(std::cout, first, "input", options.inputPath);
                    jsonFieldString(std::cout, first, "protocol", protocolToString(options.protocol));
                    jsonFieldInt(std::cout, first, "width", image.cols);
                    jsonFieldInt(std::cout, first, "height", image.rows);
                    jsonFieldDouble(std::cout, first, "step", options.step, 2);
                    jsonFieldString(std::cout, first, "layout", layoutToString(options.layout));
                    jsonFieldString(
                        std::cout,
                        first,
                        "key_mode",
                        options.key.has_value() ? "provided" : "none"
                    );
                    jsonFieldInt(std::cout, first, "capacity_bits", capacityBits);
                    jsonFieldInt(std::cout, first, "capacity_bytes", capacityBytes);
                    jsonFieldInt(std::cout, first, "message_bytes", static_cast<int>(messageBytes));
                    jsonFieldInt(std::cout, first, "required_bits", requiredBits);
                    jsonFieldDouble(std::cout, first, "utilization_percent", utilization, 2);
                    jsonFieldBool(std::cout, first, "fits", requiredBits <= capacityBits);
                    std::cout << "\n}\n";
                    return 0;
                }

                printSectionHeader(std::cout, "Classic Info");
                printKeyValue(std::cout, "Input", options.inputPath);
                printKeyValue(std::cout, "Protocol", formatProtocolLabel(options.protocol));
                printKeyValue(std::cout, "Width", image.cols);
                printKeyValue(std::cout, "Height", image.rows);
                printKeyValue(std::cout, "Step", options.step, 2);
                printKeyValue(std::cout, "Layout", layoutToString(options.layout));
                printKeyValue(std::cout, "Capacity bits", capacityBits);
                printKeyValue(std::cout, "Capacity bytes", capacityBytes);
                printKeyValue(std::cout, "Message bytes", messageBytes);
                printKeyValue(std::cout, "Required bits", requiredBits);
                printKeyValue(std::cout, "Utilization %", utilization, 2);
                printKeyValue(std::cout, "Fits", formatBoolYesNo(requiredBits <= capacityBits));
                return 0;
            }

            if (options.json) {
                bool first = true;
                std::cout << "{\n";
                jsonFieldString(std::cout, first, "status", "OK");
                jsonFieldString(std::cout, first, "input", options.inputPath);
                jsonFieldString(std::cout, first, "protocol", protocolToString(options.protocol));
                jsonFieldInt(std::cout, first, "width", image.cols);
                jsonFieldInt(std::cout, first, "height", image.rows);
                jsonFieldDouble(std::cout, first, "step", options.step, 2);
                jsonFieldString(std::cout, first, "layout", layoutToString(options.layout));
                jsonFieldString(
                    std::cout,
                    first,
                    "key_mode",
                    options.key.has_value() ? "provided" : "none"
                );
                jsonFieldInt(std::cout, first, "capacity_bits", capacityBits);
                jsonFieldInt(std::cout, first, "capacity_bytes", capacityBytes);
                std::cout << "\n}\n";
                return 0;
            }

            printSectionHeader(std::cout, "Classic Info");
            printKeyValue(std::cout, "Input", options.inputPath);
            printKeyValue(std::cout, "Protocol", formatProtocolLabel(options.protocol));
            printKeyValue(std::cout, "Width", image.cols);
            printKeyValue(std::cout, "Height", image.rows);
            printKeyValue(std::cout, "Step", options.step, 2);
            printKeyValue(std::cout, "Layout", layoutToString(options.layout));
            printKeyValue(std::cout, "Capacity bits", capacityBits);
            printKeyValue(std::cout, "Capacity bytes", capacityBytes);

            return 0;
        }

        int cmdEmbed(CommandIR &command, std::string_view argv0) {
            CommonOptions &options = command.options;
            if (resolveCommandKeySource(command) != 0) {
                return 1;
            }
            if (validateCommandRequirements(command, argv0) != 0) {
                return 1;
            }

            cv::Mat inputImage = loadImageUnchanged(options.inputPath);
            if (inputImage.empty()) {
                if (options.json) {
                    return reportCommandError(options.json, "failed to load input image: " + options.inputPath);
                }
                printHumanError(std::cerr, "failed to load input image: " + options.inputPath);
                printTryLine(
                    std::cerr,
                    std::string(argv0) + " embed --in input.jpg --out output.png --msg-block \"hello\""
                );
                return 1;
            }
            if (checkImageGuardrails(inputImage, command) != 0) {
                return 1;
            }
            if (checkMessageGuardrail(options.message, command) != 0) {
                return 1;
            }

            cv::Mat inputBGRA;
            try {
                inputBGRA = ensureBGRA(inputImage);
            } catch (const std::exception &e) {
                return reportCommandError(options.json, e.what());
            }

            const int width = inputBGRA.cols;
            const int height = inputBGRA.rows;

            cv::Mat outputBGRA(inputBGRA.rows, inputBGRA.cols, inputBGRA.type());
            const auto embedStart = std::chrono::steady_clock::now();
            roadscript::watermark::EmbedOptions embedOptions;
            embedOptions.protocol = options.protocol;
            embedOptions.step = options.step;
            embedOptions.key = options.key;
            embedOptions.preserveContrast = false;
            embedOptions.useKeyedSpatialShuffle = usesKeyedShuffle(options.layout);
            const auto embedResult = roadscript::watermark::embedBGRAWithResult(
                makeView(inputBGRA),
                makeViewMut(outputBGRA),
                options.message,
                embedOptions
            );
            const auto embedEnd = std::chrono::steady_clock::now();
            if (!embedResult.ok) {
                if (options.json) {
                    printJsonError(
                        std::cout,
                        embedResult.error.empty() ? "embedBGRA failed." : embedResult.error
                    );
                } else {
                    printHumanError(
                        std::cerr,
                        embedResult.error.empty() ? "embedBGRA failed." : embedResult.error
                    );
                }
                return 1;
            }

            cv::Mat inputBGR;
            cv::Mat outputBGR;
            try {
                inputBGR = ensureBGR(inputImage);
                outputBGR = ensureBGR(outputBGRA);
            } catch (const std::exception &e) {
                return reportCommandError(options.json, e.what());
            }

            const double snr = roadscript::watermark::SNRRatio(inputBGR, outputBGR);
            const double embedTimeMs =
                std::chrono::duration<double, std::milli>(embedEnd - embedStart).count();
            const std::string embedLayout =
                roadscript::watermark::isMosaicProtocol(options.protocol)
                    ? "mosaic-6x6-experimental"
                    : layoutToString(options.layout);
            const int reportedCapacityBits = embedResult.capacityBits;
            const int reportedCapacityBytes = reportedCapacityBits / 8;
            const int reportedRequiredBits = embedResult.requiredBits;
            const double reportedUtilization = embedResult.utilizationPercent;
            const bool reportedFits = reportedRequiredBits <= reportedCapacityBits;

            if (!cv::imwrite(options.outputPath, outputBGRA)) {
                return reportCommandError(options.json, "failed to save output image: " + options.outputPath);
            }

            if (options.json) {
                bool first = true;
                std::cout << "{\n";
                jsonFieldString(std::cout, first, "status", "OK");
                jsonFieldString(std::cout, first, "input", options.inputPath);
                jsonFieldString(std::cout, first, "output", options.outputPath);
                jsonFieldString(std::cout, first, "protocol", embedResult.protocol);
                jsonFieldInt(std::cout, first, "width", width);
                jsonFieldInt(std::cout, first, "height", height);
                jsonFieldDouble(std::cout, first, "step", options.step, 2);
                jsonFieldString(std::cout, first, "layout", embedLayout);
                jsonFieldString(
                    std::cout,
                    first,
                    "key_mode",
                    options.key.has_value() ? "provided" : "none"
                );
                jsonFieldInt(std::cout, first, "message_bytes", static_cast<int>(options.message.size()));
                jsonFieldInt(std::cout, first, "capacity_bits", reportedCapacityBits);
                jsonFieldInt(std::cout, first, "capacity_bytes", reportedCapacityBytes);
                jsonFieldInt(std::cout, first, "required_bits", reportedRequiredBits);
                jsonFieldDouble(std::cout, first, "utilization_percent", reportedUtilization, 2);
                jsonFieldBool(std::cout, first, "fits", reportedFits);
                emitMosaicEmbedJsonFields(std::cout, first, embedResult);
                jsonFieldDouble(std::cout, first, "snr", snr, 2);
                jsonFieldDouble(std::cout, first, "embed_time_ms", embedTimeMs, 2);
                std::cout << "\n}\n";
                return 0;
            }

            printSectionHeader(std::cout, "Embedding Result");
            printKeyValue(std::cout, "Protocol", formatProtocolLabel(options.protocol));
            printKeyValue(std::cout, "Input", options.inputPath);
            printKeyValue(std::cout, "Output", options.outputPath);
            printKeyValue(std::cout, "Width", width);
            printKeyValue(std::cout, "Height", height);
            printKeyValue(std::cout, "Step", options.step, 2);
            printKeyValue(std::cout, "Layout", embedLayout);
            printKeyValue(std::cout, "Key", options.key.has_value() ? "provided" : "none");
            printKeyValue(std::cout, "Message bytes", options.message.size());
            printKeyValue(std::cout, "Capacity bits", reportedCapacityBits);
            printKeyValue(std::cout, "Capacity bytes", reportedCapacityBytes);
            printKeyValue(std::cout, "Required bits", reportedRequiredBits);
            printKeyValue(std::cout, "Utilization %", reportedUtilization, 2);
            printKeyValue(std::cout, "Fits", formatBoolYesNo(reportedFits));
            printMosaicEmbedLines(std::cout, embedResult);
            printKeyValue(std::cout, "SNR", snr, 2);
            printKeyValue(std::cout, "Embedding time ms", embedTimeMs, 2);
            printKeyValue(std::cout, "Embedding", "succeeded");

            return 0;
        }

        int runDecodeLike(const std::string &labelPrefix, CommandIR &command, std::string_view argv0) {
            CommonOptions &options = command.options;
            if (resolveCommandKeySource(command) != 0) {
                return 1;
            }
            if (validateCommandRequirements(command, argv0) != 0) {
                return 1;
            }

            cv::Mat inputImage = loadImageUnchanged(options.inputPath);
            if (inputImage.empty()) {
                if (options.json) {
                    printJsonError(std::cout, "failed to load input image: " + options.inputPath);
                } else {
                    printHumanError(std::cerr, "failed to load input image: " + options.inputPath);
                    printTryLine(
                        std::cerr,
                        std::string(argv0) +
                            (labelPrefix == "extract" ? " extract --in output.png" : " verify --in output.png")
                    );
                }
                return 1;
            }
            if (checkImageGuardrails(inputImage, command) != 0) {
                return 1;
            }

            cv::Mat inputBGR;
            try {
                inputBGR = ensureBGR(inputImage);
            } catch (const std::exception &e) {
                if (options.json) {
                    printJsonError(std::cout, e.what());
                } else {
                    printHumanError(std::cerr, e.what());
                }
                return 1;
            }

            const bool useKeyedShuffle = options.layout == LayoutMode::KeyedShuffle;
            const auto decodeStart = std::chrono::steady_clock::now();
            roadscript::watermark::DecodeOptions decodeOptions;
            decodeOptions.protocol = options.protocol;
            decodeOptions.step = options.step;
            decodeOptions.key = options.key;
            decodeOptions.useKeyedSpatialShuffle = useKeyedShuffle;
            decodeOptions.enableStepSearch = options.enableStepSearch;
            roadscript::watermark::DecodeResult decoded =
                roadscript::watermark::decodeFromBGR(inputBGR, decodeOptions);
            const auto decodeEnd = std::chrono::steady_clock::now();
            const double decodeTimeMs =
                std::chrono::duration<double, std::milli>(decodeEnd - decodeStart).count();
            const std::string decodeLayout =
                roadscript::watermark::isMosaicProtocol(options.protocol)
                    ? decoded.layoutUsed
                    : layoutToString(options.layout);

            if (!options.debugJsonPath.empty()) {
                std::string debugWriteError;
                const auto payload = buildMosaicDebugJson(options, decoded, inputImage, decodeLayout);
                if (!writeDebugArtifactFile(
                        options.debugJsonPath,
                        payload,
                        "debug JSON",
                        debugWriteError
                    )) {
                    if (options.json) {
                        printJsonError(std::cout, debugWriteError);
                    } else {
                        printHumanError(std::cerr, debugWriteError);
                    }
                    return 1;
                }
            }
            if (!options.debugSvgPath.empty()) {
                std::string debugWriteError;
                const auto payload = buildMosaicDebugSvg(options, decoded, inputImage, decodeLayout);
                if (!writeDebugArtifactFile(options.debugSvgPath, payload, "debug SVG", debugWriteError)) {
                    if (options.json) {
                        printJsonError(std::cout, debugWriteError);
                    } else {
                        printHumanError(std::cerr, debugWriteError);
                    }
                    return 1;
                }
            }

            if (options.json) {
                bool first = true;
                std::cout << "{\n";
                jsonFieldString(std::cout, first, "status", decoded.ok ? "PASS" : "FAIL");
                jsonFieldString(std::cout, first, "input", options.inputPath);
                jsonFieldString(std::cout, first, "protocol", decoded.protocol);
                jsonFieldDouble(std::cout, first, "step", options.step, 2);
                jsonFieldString(std::cout, first, "layout", decodeLayout);
                jsonFieldString(
                    std::cout,
                    first,
                    "key_mode",
                    options.key.has_value() ? "provided" : "none"
                );
                jsonFieldBool(std::cout, first, "step_search", options.enableStepSearch);
                jsonFieldDouble(std::cout, first, "step_used", decoded.stepUsed, 2);
                jsonFieldBool(std::cout, first, "header_detected", decoded.headerDetected);
                jsonFieldBool(std::cout, first, "payload_parsed", decoded.payloadParsed);
                jsonFieldBool(std::cout, first, "crc_passed", decoded.crcPassed);
                jsonFieldString(std::cout, first, "failure_reason", decoded.failureReason);
                jsonFieldString(std::cout, first, "orientation_used", decoded.orientationUsed);
                jsonFieldString(std::cout, first, "layout_used", decoded.layoutUsed);
                jsonFieldInt(std::cout, first, "decode_attempts", decoded.decodeAttempts);
                emitMosaicDecodeJsonFields(std::cout, first, decoded);
                if (labelPrefix == "extract") {
                    jsonFieldDouble(std::cout, first, "extract_time_ms", decodeTimeMs, 2);
                } else {
                    jsonFieldDouble(std::cout, first, "verify_time_ms", decodeTimeMs, 2);
                }

                if (decoded.ok) {
                    jsonFieldString(std::cout, first, "decoded_message", decoded.message);
                } else {
                    jsonFieldString(std::cout, first, "error", decoded.reason);
                }

                std::cout << "\n}\n";
                return decoded.ok ? 0 : 1;
            }

            printSectionHeader(
                std::cout,
                labelPrefix == "extract" ? "Payload Recovery Result" : "Verification Result"
            );
            printKeyValue(std::cout, "Protocol", formatProtocolLabel(options.protocol));
            printKeyValue(std::cout, "Input", options.inputPath);
            printKeyValue(std::cout, "Step", options.step, 2);
            printKeyValue(std::cout, "Layout", decodeLayout);
            printKeyValue(std::cout, "Key", options.key.has_value() ? "provided" : "none");
            printKeyValue(std::cout, "Step search", formatBoolOnOff(options.enableStepSearch));
            printKeyValue(std::cout, "Step used", decoded.stepUsed, 2);
            if (!options.debugJsonPath.empty()) {
                printKeyValue(std::cout, "Debug JSON", options.debugJsonPath);
            }
            if (!options.debugSvgPath.empty()) {
                printKeyValue(std::cout, "Debug SVG", options.debugSvgPath);
            }

            if (labelPrefix == "extract") {
                printKeyValue(std::cout, "Payload recovery ms", decodeTimeMs, 2);
                printKeyValue(std::cout, "Payload Recovery", decoded.ok ? "succeeded" : "failed");
            } else {
                printKeyValue(std::cout, "Payload recovery ms", decodeTimeMs, 2);
                printKeyValue(std::cout, "Payload Recovery", decoded.ok ? "succeeded" : "failed");
                printKeyValue(std::cout, "Verification", formatPassFail(decoded.ok));
            }

            if (decoded.ok) {
                printMosaicDecodeLines(std::cout, decoded);
                printMultilineKeyValue(std::cout, "Recovered payload", decoded.message);
                return 0;
            }

            printKeyValue(std::cout, "Recovery detail", decoded.reason);
            printKeyValue(std::cout, "Failure reason", decoded.failureReason);
            printKeyValue(std::cout, "Decode attempts", decoded.decodeAttempts);
            printKeyValue(std::cout, "Orientation used", decoded.orientationUsed);
            printKeyValue(std::cout, "Layout used", decoded.layoutUsed);
            printMosaicDecodeLines(std::cout, decoded);
            return 1;
        }
    } // namespace

    int executeCommandIR(CommandIR &command, std::string_view argv0) {
        switch (command.kind) {
            case CommandKind::Version:
                printVersion(std::cout);
                return 0;
            case CommandKind::Doctor:
                printDoctor(std::cout);
                return 0;
            case CommandKind::ConfigShow:
                printConfig(std::cout);
                return 0;
            case CommandKind::Info:
                return cmdInfo(command, argv0);
            case CommandKind::Embed:
                return cmdEmbed(command, argv0);
            case CommandKind::Extract:
                return runDecodeLike("extract", command, argv0);
            case CommandKind::Verify:
                return runDecodeLike("verify", command, argv0);
        }
        return 1;
    }

    int runCli(int argc, char **argv) {
        const std::string argv0 = (argc > 0 && argv[0] != nullptr) ? argv[0] : "rse";
        if (const auto colorMode = prescanColorMode(argc, argv)) {
            setCliColorMode(*colorMode);
        } else {
            setCliColorMode(ColorMode::Auto);
        }

        try {
            if (argc >= 2 && argv[1] != nullptr && std::string_view(argv[1]) == "run") {
                ParseRunCommandResult parsedRun = parseRunCommandArgs(argc, argv, argv0);
                if (!parsedRun.parseSucceeded) {
                    return parsedRun.exitCode;
                }
                setCliColorMode(parsedRun.options.colorMode);
                return runDslWorkflow(parsedRun.options, argv0);
            }

            ParseCommandIRResult parsed = parseCommandLineToIR(argc, argv, argv0);
            if (!parsed.parseSucceeded) {
                return parsed.exitCode;
            }
            switch (parsed.command.kind) {
                case CommandKind::Info:
                case CommandKind::Embed:
                case CommandKind::Extract:
                case CommandKind::Verify:
                    setCliColorMode(parsed.command.options.colorMode);
                    break;
                case CommandKind::Version:
                case CommandKind::Doctor:
                case CommandKind::ConfigShow:
                    break;
            }
            return executeCommandIR(parsed.command, argv0);
        } catch (const std::exception &e) {
            printHumanError(std::cerr, e.what());
            return 1;
        }
    }
} // namespace roadscript::cli
