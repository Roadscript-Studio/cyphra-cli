#include "src/internal/mosaic_layout.hpp"
#include "src/internal/mosaic_packets.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {
    int require(bool condition, const std::string &message) {
        if (!condition) {
            std::cerr << "[FAIL] " << message << "\n";
            return 1;
        }
        return 0;
    }
}

int main() {
    using namespace roadscript::watermark;

    {
        const auto layout = buildMosaic6x6Layout(1024, 1024);
        int locatorCount = 0;
        int timingCount = 0;
        int metadataCount = 0;
        int dataCount = 0;
        for (const auto &cell : layout.cells) {
            switch (cell.role) {
                case MosaicCellRole::Locator: locatorCount++; break;
                case MosaicCellRole::Timing: timingCount++; break;
                case MosaicCellRole::Metadata: metadataCount++; break;
                case MosaicCellRole::Data: dataCount++; break;
            }
        }

        if (int rc = require(layout.cells.size() == 36u, "6x6 layout should have 36 cells")) return rc;
        if (int rc = require(locatorCount == 4, "locator cell count should be 4")) return rc;
        if (int rc = require(timingCount == 16, "timing cell count should be 16")) return rc;
        if (int rc = require(metadataCount == 4, "metadata cell count should be 4")) return rc;
        if (int rc = require(dataCount == 12, "data cell count should be 12")) return rc;
        if (int rc = require(layout.metadataCellIndices.size() == 4u, "metadata index count should be 4")) return rc;
        if (int rc = require(layout.dataCellIndices.size() == 12u, "data index count should be 12")) return rc;
    }

    {
        MosaicMetadataPacket packet;
        packet.rows = 6;
        packet.cols = 6;
        packet.fragmentCount = 4;
        packet.thresholdK = 4;
        packet.payloadBytes = 17;

        const auto bytes = buildMosaicMetadataPacketBytes(packet);
        const auto parsed = parseMosaicMetadataPacketBytes(bytes);
        if (int rc = require(parsed.ok, "metadata packet should parse")) return rc;
        if (int rc = require(parsed.packet.rows == packet.rows, "metadata rows mismatch")) return rc;
        if (int rc = require(parsed.packet.cols == packet.cols, "metadata cols mismatch")) return rc;
        if (int rc = require(parsed.packet.fragmentCount == packet.fragmentCount, "metadata fragmentCount mismatch")) return rc;
        if (int rc = require(parsed.packet.thresholdK == packet.thresholdK, "metadata thresholdK mismatch")) return rc;
        if (int rc = require(parsed.packet.payloadBytes == packet.payloadBytes, "metadata payloadBytes mismatch")) return rc;

        auto corrupted = bytes;
        corrupted.back() ^= 0x01u;
        const auto corruptedParsed = parseMosaicMetadataPacketBytes(corrupted);
        if (int rc = require(!corruptedParsed.ok, "corrupted metadata packet should fail")) return rc;
    }

    {
        MosaicFragmentPiece piece;
        piece.pieceId = 2;
        piece.groupId = MOSAIC_V2_GROUP_ID;
        piece.fragmentCount = 4;
        piece.thresholdK = 4;
        piece.isParity = true;
        piece.payload = {1u, 2u, 3u, 4u};

        const auto bytes = buildMosaicFragmentPacketBytes(piece);
        const auto parsed = parseMosaicFragmentPacketBytes(bytes);
        if (int rc = require(parsed.ok, "fragment packet should parse")) return rc;
        if (int rc = require(parsed.piece.pieceId == piece.pieceId, "fragment pieceId mismatch")) return rc;
        if (int rc = require(parsed.piece.groupId == piece.groupId, "fragment groupId mismatch")) return rc;
        if (int rc = require(parsed.piece.fragmentCount == piece.fragmentCount, "fragment count mismatch")) return rc;
        if (int rc = require(parsed.piece.thresholdK == piece.thresholdK, "fragment threshold mismatch")) return rc;
        if (int rc = require(parsed.piece.isParity == piece.isParity, "fragment parity flag mismatch")) return rc;
        if (int rc = require(parsed.piece.payload == piece.payload, "fragment payload mismatch")) return rc;

        auto corrupted = bytes;
        corrupted[5] ^= 0x04u;
        const auto corruptedParsed = parseMosaicFragmentPacketBytes(corrupted);
        if (int rc = require(!corruptedParsed.ok, "corrupted fragment packet should fail")) return rc;
    }

    {
        const auto body = buildMosaicPayloadBody("roadscript-v3");
        const auto parsed = parseMosaicPayloadBody(body);
        if (int rc = require(parsed.ok, "payload body should parse")) return rc;
        if (int rc = require(parsed.message == "roadscript-v3", "payload body message mismatch")) return rc;
    }

    {
        const auto body = buildMosaicPayloadBody("roadscript-v3");
        const auto sourceFragments = splitMosaicPayloadBodyIntoEqualPaddedFragments(body, 4);
        const auto parityFragments = buildMosaicParityPayloads(sourceFragments);

        std::vector<std::optional<std::vector<std::uint8_t>>> sources(4);
        sources[0] = sourceFragments[0];
        sources[1] = sourceFragments[1];
        sources[3] = sourceFragments[3];

        std::vector<std::optional<std::vector<std::uint8_t>>> parity(4);
        parity[1] = parityFragments[1];

        int recoveredCount = 0;
        const bool recoveredAny = recoverMosaicSourceFragmentsViaParity(sources, parity, recoveredCount);
        if (int rc = require(recoveredAny, "parity recovery should recover a missing source")) return rc;
        if (int rc = require(recoveredCount == 4, "all four sources should be present after parity recovery")) return rc;

        std::vector<std::uint8_t> reconstructed;
        for (const auto &fragment : sources) {
            reconstructed.insert(reconstructed.end(), fragment->begin(), fragment->end());
        }
        reconstructed.resize(body.size());
        const auto parsed = parseMosaicPayloadBody(reconstructed);
        if (int rc = require(parsed.ok, "reconstructed payload body should pass CRC32")) return rc;
        if (int rc = require(parsed.message == "roadscript-v3", "reconstructed message mismatch")) return rc;
    }

    std::cout << "[PASS] mosaic_helpers_smoke\n";
    return 0;
}
