#include "iq_header.h"
#include <cstring>

namespace kraken_doa::heimdall {

    namespace {
        uint32_t rd32(const uint8_t* b, size_t o) {
            return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
        }
        uint64_t rd64(const uint8_t* b, size_t o) {
            return (uint64_t)rd32(b, o) | ((uint64_t)rd32(b, o + 4) << 32);
        }
        void wr32(uint8_t* b, size_t o, uint32_t v) {
            for (int i = 0; i < 4; i++) { b[o + i] = (uint8_t)(v >> (8 * i)); }
        }
        void wr64(uint8_t* b, size_t o, uint64_t v) {
            for (int i = 0; i < 8; i++) { b[o + i] = (uint8_t)(v >> (8 * i)); }
        }
        bool fail(std::string* err, const std::string& msg) {
            if (err) { *err = msg; }
            return false;
        }
    }

    bool parseHeader(const uint8_t* buf, size_t len, IqHeader& out, std::string* err, uint64_t maxPayloadBytes) {
        if (!buf || len < HEADER_SIZE) { return fail(err, "header too short"); }

        IqHeader h;
        h.syncWord = rd32(buf, off::SYNC_WORD);
        if (h.syncWord != SYNC_WORD) { return fail(err, "bad sync word"); }
        h.frameType = rd32(buf, off::FRAME_TYPE);
        std::memcpy(h.hardwareId, buf + off::HARDWARE_ID, 16);
        h.hardwareId[16] = 0;
        h.unitId = rd32(buf, off::UNIT_ID);
        h.activeAntChs = rd32(buf, off::ACTIVE_ANT_CHS);
        h.iooType = rd32(buf, off::IOO_TYPE);
        h.rfCenterFreq = rd64(buf, off::RF_CENTER_FREQ);
        h.adcSamplingFreq = rd64(buf, off::ADC_SAMPLING_FREQ);
        h.samplingFreq = rd64(buf, off::SAMPLING_FREQ);
        h.cpiLength = rd32(buf, off::CPI_LENGTH);
        h.timeStamp = rd64(buf, off::TIME_STAMP);
        h.daqBlockIndex = rd32(buf, off::DAQ_BLOCK_INDEX);
        h.cpiIndex = rd32(buf, off::CPI_INDEX);
        h.extIntegrationCntr = rd64(buf, off::EXT_INTEGRATION_CNTR);
        h.dataType = rd32(buf, off::DATA_TYPE);
        h.sampleBitDepth = rd32(buf, off::SAMPLE_BIT_DEPTH);
        h.adcOverdriveFlags = rd32(buf, off::ADC_OVERDRIVE_FLAGS);
        for (size_t i = 0; i < 32; i++) { h.ifGains[i] = rd32(buf, off::IF_GAINS + 4 * i); }
        h.delaySyncFlag = rd32(buf, off::DELAY_SYNC_FLAG);
        h.iqSyncFlag = rd32(buf, off::IQ_SYNC_FLAG);
        h.syncState = rd32(buf, off::SYNC_STATE);
        h.noiseSourceState = rd32(buf, off::NOISE_SOURCE_STATE);
        for (size_t i = 0; i < 192; i++) { h.reserved[i] = rd32(buf, off::RESERVED + 4 * i); }
        h.headerVersion = rd32(buf, off::HEADER_VERSION);

        if (h.activeAntChs == 0 || h.activeAntChs > MAX_CHANNELS) {
            return fail(err, "invalid active_ant_chs " + std::to_string(h.activeAntChs));
        }
        uint64_t payload = h.payloadBytes();
        if (payload > maxPayloadBytes) {
            return fail(err, "payload of " + std::to_string(payload) + " bytes exceeds limit");
        }
        if (payload > 0 && h.sampleBitDepth != 32) {
            return fail(err, "unsupported sample_bit_depth " + std::to_string(h.sampleBitDepth));
        }
        out = h;
        return true;
    }

    void serializeHeader(const IqHeader& h, uint8_t out[1024]) {
        std::memset(out, 0, HEADER_SIZE);
        wr32(out, off::SYNC_WORD, h.syncWord);
        wr32(out, off::FRAME_TYPE, h.frameType);
        std::memcpy(out + off::HARDWARE_ID, h.hardwareId, 16);
        wr32(out, off::UNIT_ID, h.unitId);
        wr32(out, off::ACTIVE_ANT_CHS, h.activeAntChs);
        wr32(out, off::IOO_TYPE, h.iooType);
        wr64(out, off::RF_CENTER_FREQ, h.rfCenterFreq);
        wr64(out, off::ADC_SAMPLING_FREQ, h.adcSamplingFreq);
        wr64(out, off::SAMPLING_FREQ, h.samplingFreq);
        wr32(out, off::CPI_LENGTH, h.cpiLength);
        wr64(out, off::TIME_STAMP, h.timeStamp);
        wr32(out, off::DAQ_BLOCK_INDEX, h.daqBlockIndex);
        wr32(out, off::CPI_INDEX, h.cpiIndex);
        wr64(out, off::EXT_INTEGRATION_CNTR, h.extIntegrationCntr);
        wr32(out, off::DATA_TYPE, h.dataType);
        wr32(out, off::SAMPLE_BIT_DEPTH, h.sampleBitDepth);
        wr32(out, off::ADC_OVERDRIVE_FLAGS, h.adcOverdriveFlags);
        for (size_t i = 0; i < 32; i++) { wr32(out, off::IF_GAINS + 4 * i, h.ifGains[i]); }
        wr32(out, off::DELAY_SYNC_FLAG, h.delaySyncFlag);
        wr32(out, off::IQ_SYNC_FLAG, h.iqSyncFlag);
        wr32(out, off::SYNC_STATE, h.syncState);
        wr32(out, off::NOISE_SOURCE_STATE, h.noiseSourceState);
        for (size_t i = 0; i < 192; i++) { wr32(out, off::RESERVED + 4 * i, h.reserved[i]); }
        wr32(out, off::HEADER_VERSION, h.headerVersion);
    }

    bool isUsableForDoa(const IqHeader& h) {
        return h.syncWord == SYNC_WORD && h.frameType == FRAME_DATA && h.delaySyncFlag == 1 && h.iqSyncFlag == 1;
    }
}
