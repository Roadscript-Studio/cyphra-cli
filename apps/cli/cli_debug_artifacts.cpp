#include "cli_debug_artifacts.hpp"

#include "cli_parse.hpp"
#include "cli_print.hpp"

#include "roadscript/watermark/api.hpp"
#include "roadscript/watermark/mosaic_debug.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string>

namespace roadscript::cli {
    namespace {
        void emitDebugCellJson(std::ostream &out, const roadscript::watermark::MosaicDebugCell &cell) {
            bool first = true;
            out << "    {\n";
            jsonFieldInt(out, first, "index", cell.index);
            jsonFieldInt(out, first, "row", cell.row);
            jsonFieldInt(out, first, "col", cell.col);
            jsonFieldString(out, first, "role", cell.role);
            jsonFieldString(out, first, "packet_kind", cell.packetKind);
            if (cell.packetId >= 0) {
                jsonFieldInt(out, first, "packet_id", cell.packetId);
            } else {
                jsonFieldNull(out, first, "packet_id");
            }
            jsonFieldBool(out, first, "crc_passed", cell.crcPassed);
            jsonFieldBool(out, first, "local_search_used", cell.localSearchUsed);
            jsonFieldBool(out, first, "recovered", cell.recovered);
            if (!cell.note.empty()) {
                jsonFieldString(out, first, "note", cell.note);
            } else {
                jsonFieldNull(out, first, "note");
            }
            out << "\n    }";
        }

        template <typename ItemFn>
        void emitDebugObjectArray(
            std::ostream &out,
            bool &first,
            std::string_view key,
            int count,
            ItemFn &&emitItem
        ) {
            if (!first) out << ",\n";
            first = false;
            out << "  " << jsonString(key) << ": [\n";
            for (int i = 0; i < count; ++i) {
                if (i != 0) {
                    out << ",\n";
                }
                emitItem(out, i);
            }
            out << "\n  ]";
        }
    } // namespace

    std::string buildMosaicDebugJson(
        const CommonOptions &options,
        const roadscript::watermark::DecodeResult &decoded,
        const cv::Mat &image,
        std::string_view layout
    ) {
        std::ostringstream out;
        bool first = true;
        out << "{\n";
        jsonFieldString(out, first, "protocol", decoded.protocol);
        jsonFieldString(out, first, "input", options.inputPath);
        jsonFieldString(out, first, "status", decoded.ok ? "PASS" : "FAIL");
        jsonFieldString(out, first, "failure_reason", decoded.failureReason);
        jsonFieldInt(out, first, "width", image.cols);
        jsonFieldInt(out, first, "height", image.rows);
        jsonFieldString(out, first, "layout", layout);
        jsonFieldDouble(out, first, "step", options.step, 2);
        jsonFieldBool(out, first, "local_search", decoded.localSearchEnabled);
        jsonFieldBool(out, first, "metadata_recovered", decoded.metadataRecovered);
        jsonFieldInt(out, first, "valid_fragments", decoded.validFragments);
        jsonFieldInt(out, first, "unique_fragments", decoded.uniqueFragments);
        jsonFieldInt(out, first, "valid_parity_fragments", decoded.validParityFragments);
        jsonFieldInt(out, first, "recovered_via_parity", decoded.recoveredSourceFragmentsViaParity);

        if (roadscript::watermark::isMosaicProtocol(options.protocol) && decoded.mosaicDebug) {
            const auto &debug = *decoded.mosaicDebug;
            jsonFieldInt(out, first, "grid_rows", debug.gridRows);
            jsonFieldInt(out, first, "grid_cols", debug.gridCols);
            emitDebugObjectArray(
                out,
                first,
                "cells",
                static_cast<int>(debug.cells.size()),
                [&](std::ostream &arrayOut, int index) {
                    emitDebugCellJson(arrayOut, debug.cells[static_cast<std::size_t>(index)]);
                }
            );
            emitDebugObjectArray(
                out,
                first,
                "metadata_copies",
                static_cast<int>(debug.metadataCopies.size()),
                [&](std::ostream &arrayOut, int index) {
                    const auto &copy = debug.metadataCopies[static_cast<std::size_t>(index)];
                    bool itemFirst = true;
                    arrayOut << "    {\n";
                    jsonFieldInt(arrayOut, itemFirst, "index", copy.index);
                    jsonFieldString(arrayOut, itemFirst, "source", copy.source);
                    jsonFieldInt(arrayOut, itemFirst, "host_cell_index", copy.hostCellIndex);
                    jsonFieldBool(arrayOut, itemFirst, "crc_passed", copy.crcPassed);
                    jsonFieldBool(arrayOut, itemFirst, "local_search_used", copy.localSearchUsed);
                    if (!copy.note.empty()) {
                        jsonFieldString(arrayOut, itemFirst, "note", copy.note);
                    } else {
                        jsonFieldNull(arrayOut, itemFirst, "note");
                    }
                    arrayOut << "\n    }";
                }
            );
            emitDebugObjectArray(
                out,
                first,
                "source_fragments",
                static_cast<int>(debug.sourceFragments.size()),
                [&](std::ostream &arrayOut, int index) {
                    const auto &fragment = debug.sourceFragments[static_cast<std::size_t>(index)];
                    bool itemFirst = true;
                    arrayOut << "    {\n";
                    jsonFieldInt(arrayOut, itemFirst, "fragment_id", fragment.fragmentId);
                    jsonFieldBool(arrayOut, itemFirst, "present", fragment.present);
                    jsonFieldBool(arrayOut, itemFirst, "recovered_via_parity", fragment.recoveredViaParity);
                    if (fragment.sourceCellIndex >= 0) {
                        jsonFieldInt(arrayOut, itemFirst, "source_cell_index", fragment.sourceCellIndex);
                    } else {
                        jsonFieldNull(arrayOut, itemFirst, "source_cell_index");
                    }
                    if (!fragment.note.empty()) {
                        jsonFieldString(arrayOut, itemFirst, "note", fragment.note);
                    } else {
                        jsonFieldNull(arrayOut, itemFirst, "note");
                    }
                    arrayOut << "\n    }";
                }
            );
            emitDebugObjectArray(
                out,
                first,
                "parity_fragments",
                static_cast<int>(debug.parityFragments.size()),
                [&](std::ostream &arrayOut, int index) {
                    const auto &parity = debug.parityFragments[static_cast<std::size_t>(index)];
                    bool itemFirst = true;
                    arrayOut << "    {\n";
                    jsonFieldInt(arrayOut, itemFirst, "parity_id", parity.parityId);
                    jsonFieldBool(arrayOut, itemFirst, "present", parity.present);
                    if (parity.sourceCellIndex >= 0) {
                        jsonFieldInt(arrayOut, itemFirst, "source_cell_index", parity.sourceCellIndex);
                    } else {
                        jsonFieldNull(arrayOut, itemFirst, "source_cell_index");
                    }
                    if (!parity.note.empty()) {
                        jsonFieldString(arrayOut, itemFirst, "note", parity.note);
                    } else {
                        jsonFieldNull(arrayOut, itemFirst, "note");
                    }
                    arrayOut << "\n    }";
                }
            );
            emitDebugObjectArray(
                out,
                first,
                "parity_recoveries",
                static_cast<int>(debug.parityRecoveries.size()),
                [&](std::ostream &arrayOut, int index) {
                    const auto &recovery = debug.parityRecoveries[static_cast<std::size_t>(index)];
                    bool itemFirst = true;
                    arrayOut << "    {\n";
                    jsonFieldInt(arrayOut, itemFirst, "fragment_id", recovery.fragmentId);
                    jsonFieldBool(arrayOut, itemFirst, "recovered", recovery.recovered);
                    if (!recovery.note.empty()) {
                        jsonFieldString(arrayOut, itemFirst, "note", recovery.note);
                    } else {
                        jsonFieldNull(arrayOut, itemFirst, "note");
                    }
                    arrayOut << "\n    }";
                }
            );
        } else {
            jsonFieldInt(out, first, "grid_rows", 0);
            jsonFieldInt(out, first, "grid_cols", 0);
            jsonFieldBool(out, first, "supported", false);
            jsonFieldString(out, first, "reason", "debug maps are only available for mosaic protocol");
            emitDebugObjectArray(out, first, "cells", 0, [](std::ostream &, int) {});
        }

        out << "\n}\n";
        return out.str();
    }

    std::string buildMosaicDebugSvg(
        const CommonOptions &options,
        const roadscript::watermark::DecodeResult &decoded,
        const cv::Mat &,
        std::string_view layout
    ) {
        constexpr int kOuterMargin = 32;
        constexpr int kBottomPadding = 36;
        constexpr int kHeaderTop = 28;
        constexpr int kHeaderHeight = 108;
        constexpr int kContentTop = 156;
        constexpr int kHeaderInnerLeft = 60;
        constexpr int kCardGap = 28;
        constexpr int kCardPaddingX = 24;
        constexpr int kCardPaddingTop = 26;
        constexpr int kGridSectionGap = 26;
        constexpr int kCellSize = 108;
        constexpr int kCellGap = 10;
        constexpr int kPanelWidth = 412;
        constexpr const char *kSansStack =
            "Inter, SF Pro Display, SF Pro Text, -apple-system, BlinkMacSystemFont, Segoe UI, sans-serif";
        constexpr const char *kMonoStack =
            "SFMono-Regular, ui-monospace, Menlo, Consolas, monospace";

        auto roleFill = [&](const roadscript::watermark::MosaicDebugCell &cell) -> const char * {
            if (cell.role == "locator") return "#cbd5e1";
            if (cell.role == "timing") return "#e5e7eb";
            if (cell.role == "metadata") return "#c7d2fe";
            if (cell.packetKind == "parity_fragment") return "#ddd6fe";
            if (cell.packetKind == "source_fragment") return "#d1fae5";
            return "#f3f4f6";
        };

        auto statusStroke = [&](const roadscript::watermark::MosaicDebugCell &cell) -> const char * {
            if (cell.recovered) {
                if (cell.localSearchUsed) {
                    return "#eab308";
                }
                return "#16a34a";
            }
            if (cell.packetKind != "none") {
                return "#dc2626";
            }
            return "#94a3b8";
        };

        auto statusSymbol = [&](const roadscript::watermark::MosaicDebugCell &cell) -> std::string {
            if (cell.recovered) {
                return cell.localSearchUsed ? "S" : "✓";
            }
            if (cell.packetKind != "none") {
                return "×";
            }
            return "•";
        };

        auto roleSymbol = [](const roadscript::watermark::MosaicDebugCell &cell) -> std::string {
            if (cell.role == "locator") return "L";
            if (cell.role == "timing") return "T";
            if (cell.role == "metadata") return "M";
            return "D";
        };

        auto packetLabel = [](const roadscript::watermark::MosaicDebugCell &cell) -> std::string {
            if (cell.packetKind == "source_fragment" && cell.packetId >= 0) {
                return "F" + std::to_string(cell.packetId);
            }
            if (cell.packetKind == "parity_fragment" && cell.packetId >= 0) {
                return "P" + std::to_string(cell.packetId);
            }
            if (cell.packetKind == "metadata") {
                return "meta";
            }
            return "";
        };

        std::ostringstream out;
        const std::string inputLabel = xmlEscape(std::filesystem::path(options.inputPath).filename().string());
        const std::string statusLabel = decoded.ok ? "PASS" : "FAIL";
        const std::string statusFill = decoded.ok ? "#dcfce7" : "#fee2e2";
        const std::string statusStrokeColor = decoded.ok ? "#22c55e" : "#ef4444";
        const std::string statusText = decoded.ok ? "#166534" : "#991b1b";
        const int gridWidth = (6 * kCellSize) + (5 * kCellGap);
        const int gridHeight = (6 * kCellSize) + (5 * kCellGap);
        const int leftCardX = kOuterMargin;
        const int leftCardTitleY = kContentTop + 40;
        const int leftCardSubtitleY = kContentTop + 64;
        const int gridLeft = leftCardX + kCardPaddingX;
        const int gridTop = leftCardSubtitleY + kGridSectionGap;
        const int leftCardWidth = (2 * kCardPaddingX) + gridWidth;
        const int panelLeft = leftCardX + leftCardWidth + kCardGap;
        const int leftCardBottom = gridTop + gridHeight + kCardPaddingTop;
        const int roleLegendBottom = 254 + static_cast<int>(5 - 1) * 34 + 22;
        const int statusLegendBottom = 460 + static_cast<int>(5 - 1) * 34 + 22;
        const int summaryBottom = 690 + static_cast<int>(6 - 1) * 32;
        const int rightPanelBottom = std::max({roleLegendBottom, statusLegendBottom, summaryBottom, 910}) + 30;
        const int contentBottom = std::max(leftCardBottom, rightPanelBottom);
        const int contentHeight = contentBottom - kContentTop;
        const int canvasHeight = contentBottom + kBottomPadding;
        const int canvasWidth = panelLeft + kPanelWidth + kOuterMargin;
        const int pageWidth = canvasWidth - (2 * kOuterMargin);
        const int headerWidth = pageWidth;

        out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << canvasWidth << "\" height=\""
            << canvasHeight << "\" viewBox=\"0 0 " << canvasWidth << " " << canvasHeight << "\">\n";
        out << "  <defs>\n"
            << "    <filter id=\"cardShadow\" x=\"-20%\" y=\"-20%\" width=\"140%\" height=\"140%\">\n"
            << "      <feDropShadow dx=\"0\" dy=\"10\" stdDeviation=\"18\" flood-color=\"#0f172a\" flood-opacity=\"0.08\"/>\n"
            << "    </filter>\n"
            << "  </defs>\n";
        out << "  <rect x=\"0\" y=\"0\" width=\"" << canvasWidth << "\" height=\"" << canvasHeight
            << "\" fill=\"#f8fafc\"/>\n";
        out << "  <rect x=\"" << kOuterMargin << "\" y=\"" << kHeaderTop << "\" width=\"" << headerWidth
            << "\" height=\"" << kHeaderHeight
            << "\" rx=\"22\" fill=\"#ffffff\" filter=\"url(#cardShadow)\"/>\n";
        out << "  <text x=\"" << kHeaderInnerLeft
            << "\" y=\"64\" font-family=\"" << kSansStack
            << "\" font-size=\"30\" font-weight=\"700\" fill=\"#0f172a\">Roadscript Mosaic Inspector</text>\n";
        out << "  <text x=\"" << kHeaderInnerLeft
            << "\" y=\"92\" font-family=\"" << kSansStack
            << "\" font-size=\"15\" fill=\"#475569\">Distributed fragment decode telemetry for the 6x6 Mosaic architecture</text>\n";
        const int statusBadgeX = canvasWidth - kOuterMargin - 256;
        out << "  <rect x=\"" << statusBadgeX
            << "\" y=\"46\" width=\"98\" height=\"34\" rx=\"17\" fill=\"" << statusFill
            << "\" stroke=\"" << statusStrokeColor << "\" stroke-width=\"1.5\"/>\n";
        out << "  <text x=\"" << (statusBadgeX + 49)
            << "\" y=\"68\" text-anchor=\"middle\" font-family=\"" << kSansStack
            << "\" font-size=\"14\" font-weight=\"700\" fill=\"" << statusText << "\">" << statusLabel
            << "</text>\n";
        out << "  <text x=\"" << kHeaderInnerLeft
            << "\" y=\"118\" font-family=\"" << kMonoStack
            << "\" font-size=\"13\" fill=\"#64748b\">input: " << inputLabel << "   layout: "
            << xmlEscape(std::string(layout)) << "   step: " << options.step << "   failure: "
            << xmlEscape(decoded.failureReason) << "</text>\n";

        out << "  <rect x=\"" << kOuterMargin << "\" y=\"" << kContentTop << "\" width=\"" << leftCardWidth
            << "\" height=\"" << contentHeight << "\" rx=\"22\" fill=\"#ffffff\" filter=\"url(#cardShadow)\"/>\n";
        out << "  <text x=\"" << (leftCardX + kCardPaddingX)
            << "\" y=\"" << leftCardTitleY << "\" font-family=\"" << kSansStack
            << "\" font-size=\"20\" font-weight=\"700\" fill=\"#0f172a\">Cell map</text>\n";
        out << "  <text x=\"" << (leftCardX + kCardPaddingX)
            << "\" y=\"" << leftCardSubtitleY << "\" font-family=\"" << kSansStack
            << "\" font-size=\"13\" fill=\"#64748b\">Role fill shows architecture responsibilities. Border and badge show recovery outcome.</text>\n";

        out << "  <rect x=\"" << panelLeft << "\" y=\"" << kContentTop << "\" width=\"" << kPanelWidth
            << "\" height=\"" << contentHeight << "\" rx=\"22\" fill=\"#ffffff\" filter=\"url(#cardShadow)\"/>\n";
        out << "  <text x=\"" << (panelLeft + 24)
            << "\" y=\"196\" font-family=\"" << kSansStack
            << "\" font-size=\"20\" font-weight=\"700\" fill=\"#0f172a\">Legend</text>\n";

        if (roadscript::watermark::isMosaicProtocol(options.protocol) && decoded.mosaicDebug) {
            const auto &debug = *decoded.mosaicDebug;
            for (const auto &cell : debug.cells) {
                const int x = gridLeft + cell.col * (kCellSize + kCellGap);
                const int y = gridTop + cell.row * (kCellSize + kCellGap);
                const int cellInner = kCellSize;
                const std::string title =
                    "row=" + std::to_string(cell.row) +
                    ", col=" + std::to_string(cell.col) +
                    ", role=" + cell.role +
                    ", packet_kind=" + cell.packetKind +
                    ", packet_id=" +
                    (cell.packetId >= 0 ? std::to_string(cell.packetId) : std::string("none")) +
                    ", crc_passed=" + std::string(cell.crcPassed ? "true" : "false") +
                    ", local_search_used=" + std::string(cell.localSearchUsed ? "true" : "false") +
                    ", recovered=" + std::string(cell.recovered ? "true" : "false") +
                    ", note=" + (cell.note.empty() ? std::string("none") : cell.note);
                out << "  <g>\n"
                    << "    <title>" << xmlEscape(title) << "</title>\n"
                    << "    <rect x=\"" << x << "\" y=\"" << y << "\" width=\"" << cellInner
                    << "\" height=\"" << cellInner << "\" rx=\"18\" fill=\"" << roleFill(cell)
                    << "\" stroke=\"" << statusStroke(cell) << "\" stroke-width=\"3\"/>\n"
                    << "    <circle cx=\"" << (x + cellInner - 18) << "\" cy=\"" << (y + 18)
                    << "\" r=\"13\" fill=\"#ffffff\" stroke=\"" << statusStroke(cell)
                    << "\" stroke-width=\"2\"/>\n"
                    << "    <text x=\"" << (x + cellInner - 18) << "\" y=\"" << (y + 23)
                    << "\" text-anchor=\"middle\" font-family=\"" << kSansStack
                    << "\" font-size=\"14\" font-weight=\"700\" fill=\"" << statusStroke(cell)
                    << "\">" << xmlEscape(statusSymbol(cell)) << "</text>\n"
                    << "    <text x=\"" << (x + cellInner / 2) << "\" y=\"" << (y + 40)
                    << "\" text-anchor=\"middle\" font-family=\"" << kSansStack
                    << "\" font-size=\"26\" font-weight=\"700\" fill=\"#0f172a\">"
                    << xmlEscape(roleSymbol(cell)) << "</text>\n";
                const std::string label = packetLabel(cell);
                if (!label.empty()) {
                    out << "    <text x=\"" << (x + cellInner / 2) << "\" y=\"" << (y + 66)
                        << "\" text-anchor=\"middle\" font-family=\"" << kMonoStack
                        << "\" font-size=\"15\" font-weight=\"600\" fill=\"#334155\">"
                        << xmlEscape(label) << "</text>\n";
                }
                out << "    <text x=\"" << (x + cellInner / 2) << "\" y=\"" << (y + 88)
                    << "\" text-anchor=\"middle\" font-family=\"" << kSansStack
                    << "\" font-size=\"11\" fill=\"#475569\">r" << cell.row << " c" << cell.col
                    << "</text>\n"
                    << "  </g>\n";
            }
        } else {
            out << "  <text x=\"" << (leftCardX + kCardPaddingX)
                << "\" y=\"" << (gridTop + 40) << "\" font-family=\"" << kSansStack
                << "\" font-size=\"16\" fill=\"#991b1b\">Debug map available only for mosaic protocol.</text>\n";
        }

        out << "  <text x=\"" << (panelLeft + 24)
            << "\" y=\"236\" font-family=\"" << kSansStack
            << "\" font-size=\"16\" font-weight=\"700\" fill=\"#0f172a\">Cell roles</text>\n";
        struct RoleLegendRow {
            const char *fill;
            const char *label;
            const char *symbol;
        };
        const std::array<RoleLegendRow, 5> roleLegendRows{{
            {"#cbd5e1", "Locator", "L"},
            {"#e5e7eb", "Timing", "T"},
            {"#c7d2fe", "Metadata", "M"},
            {"#d1fae5", "Source fragment", "D"},
            {"#ddd6fe", "Parity fragment", "D"},
        }};
        for (std::size_t i = 0; i < roleLegendRows.size(); ++i) {
            const int y = 254 + static_cast<int>(i) * 34;
            out << "  <rect x=\"" << (panelLeft + 24) << "\" y=\"" << y
                << "\" width=\"22\" height=\"22\" rx=\"6\" fill=\"" << roleLegendRows[i].fill
                << "\" stroke=\"#94a3b8\" stroke-width=\"1.2\"/>\n"
                << "  <text x=\"" << (panelLeft + 35) << "\" y=\"" << (y + 16)
                << "\" text-anchor=\"middle\" font-family=\"" << kSansStack
                << "\" font-size=\"12\" font-weight=\"700\" fill=\"#0f172a\">"
                << roleLegendRows[i].symbol << "</text>\n"
                << "  <text x=\"" << (panelLeft + 58) << "\" y=\"" << (y + 16)
                << "\" font-family=\"" << kSansStack << "\" font-size=\"14\" fill=\"#0f172a\">"
                << roleLegendRows[i].label << "</text>\n";
        }

        out << "  <text x=\"" << (panelLeft + 24)
            << "\" y=\"442\" font-family=\"" << kSansStack
            << "\" font-size=\"16\" font-weight=\"700\" fill=\"#0f172a\">Recovery status</text>\n";
        struct StatusLegendRow {
            const char *stroke;
            const char *symbol;
            const char *label;
        };
        const std::array<StatusLegendRow, 5> statusLegendRows{{
            {"#16a34a", "✓", "CRC passed / recovered"},
            {"#eab308", "S", "Recovered by local search"},
            {"#a855f7", "P", "Recovered via parity"},
            {"#dc2626", "×", "Failed / CRC failed"},
            {"#94a3b8", "•", "Neutral structural cell"},
        }};
        for (std::size_t i = 0; i < statusLegendRows.size(); ++i) {
            const int y = 460 + static_cast<int>(i) * 34;
            out << "  <circle cx=\"" << (panelLeft + 35) << "\" cy=\"" << (y + 11)
                << "\" r=\"11\" fill=\"#ffffff\" stroke=\"" << statusLegendRows[i].stroke
                << "\" stroke-width=\"2\"/>\n"
                << "  <text x=\"" << (panelLeft + 35) << "\" y=\"" << (y + 16)
                << "\" text-anchor=\"middle\" font-family=\"" << kSansStack
                << "\" font-size=\"12\" font-weight=\"700\" fill=\"" << statusLegendRows[i].stroke
                << "\">" << xmlEscape(statusLegendRows[i].symbol) << "</text>\n"
                << "  <text x=\"" << (panelLeft + 58) << "\" y=\"" << (y + 16)
                << "\" font-family=\"" << kSansStack << "\" font-size=\"14\" fill=\"#0f172a\">"
                << xmlEscape(statusLegendRows[i].label) << "</text>\n";
        }

        out << "  <text x=\"" << (panelLeft + 24)
            << "\" y=\"668\" font-family=\"" << kSansStack
            << "\" font-size=\"16\" font-weight=\"700\" fill=\"#0f172a\">Summary stats</text>\n";
        struct SummaryRow {
            const char *label;
            std::string value;
        };
        const std::array<SummaryRow, 6> summaryRows{{
            {"metadata recovered", decoded.metadataRecovered ? "yes" : "no"},
            {"valid fragments", std::to_string(decoded.validFragments)},
            {"unique fragments", std::to_string(decoded.uniqueFragments)},
            {"valid parity fragments", std::to_string(decoded.validParityFragments)},
            {"recovered via parity", std::to_string(decoded.recoveredSourceFragmentsViaParity)},
            {"local search", decoded.localSearchEnabled ? "on" : "off"},
        }};
        for (std::size_t i = 0; i < summaryRows.size(); ++i) {
            const int y = 690 + static_cast<int>(i) * 32;
            out << "  <text x=\"" << (panelLeft + 24) << "\" y=\"" << y
                << "\" font-family=\"" << kSansStack << "\" font-size=\"14\" fill=\"#475569\">"
                << xmlEscape(summaryRows[i].label) << "</text>\n"
                << "  <text x=\"" << (panelLeft + kPanelWidth - 24) << "\" y=\"" << y
                << "\" text-anchor=\"end\" font-family=\"" << kMonoStack
                << "\" font-size=\"14\" font-weight=\"600\" fill=\"#0f172a\">"
                << xmlEscape(summaryRows[i].value) << "</text>\n";
        }

        out << "  <text x=\"" << (panelLeft + 24)
            << "\" y=\"910\" font-family=\"" << kSansStack
            << "\" font-size=\"13\" fill=\"#64748b\">Role labels: L/T/M/D = locator / timing / metadata / data</text>\n";
        out << "</svg>\n";
        return out.str();
    }

    bool writeDebugArtifactFile(
        const std::string &path,
        const std::string &payload,
        std::string_view label,
        std::string &error
    ) {
        std::ofstream file(path, std::ios::binary);
        if (!file) {
            error = "failed to open " + std::string(label) + " path: " + path;
            return false;
        }
        file << payload;
        if (!file.good()) {
            error = "failed to write " + std::string(label) + " path: " + path;
            return false;
        }
        return true;
    }
} // namespace roadscript::cli
