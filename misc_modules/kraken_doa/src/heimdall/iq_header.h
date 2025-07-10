#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace kraken_doa::heimdall {

    constexpr uint32_t SYNC_WORD = 0x2bf7b95a;
    constexpr size_t HEADER_SIZE = 1024;
    constexpr size_t MAX_CHANNELS = 32;
    constexpr uint64_t DEFAULT_MAX_PAYLOAD_BYTES = 512ull * 1024 * 1024;

    enum FrameType : uint32_t {
        FRAME_DATA = 0,
        FRAME_DUMMY = 1,
        FRAME_RAMP = 2,
        FRAME_CAL = 3,
        FRAME_TRIGW = 4,
    };

    // Field offsets in the 1024 byte wire header (little endian, natural C alignment).
    namespace off {
        constexpr size_t SYNC_WORD = 0, FRAME_TYPE = 4, HARDWARE_ID = 8, UNIT_ID = 24,
                         ACTIVE_ANT_CHS = 28, IOO_TYPE = 32, RF_CENTER_FREQ = 40,
                         ADC_SAMPLING_FREQ = 48, SAMPLING_FREQ = 56, CPI_LENGTH = 64,
                         TIME_STAMP = 72, DAQ_BLOCK_INDEX = 80, CPI_INDEX = 84,
                         EXT_INTEGRATION_CNTR = 88, DATA_TYPE = 96, SAMPLE_BIT_DEPTH = 100,
                         ADC_OVERDRIVE_FLAGS = 104, IF_GAINS = 108, DELAY_SYNC_FLAG = 236,
                         IQ_SYNC_FLAG = 240, SYNC_STATE = 244, NOISE_SOURCE_STATE = 248,
                         RESERVED = 252, HEADER_VERSION = 1020;
    }

    struct IqHeader {
        uint32_t syncWord = SYNC_WORD;
        uint32_t frameType = FRAME_DATA;
        char hardwareId[17] = { 0 }; // NUL terminated copy of the 16 byte field
        uint32_t unitId = 0;
        uint32_t activeAntChs = 0;
        uint32_t iooType = 0;
        uint64_t rfCenterFreq = 0;
        uint64_t adcSamplingFreq = 0;
        uint64_t samplingFreq = 0;
        uint32_t cpiLength = 0;
        uint64_t timeStamp = 0;
        uint32_t daqBlockIndex = 0;
        uint32_t cpiIndex = 0;
        uint64_t extIntegrationCntr = 0;
        uint32_t dataType = 0;
        uint32_t sampleBitDepth = 32;
        uint32_t adcOverdriveFlags = 0;
        uint32_t ifGains[32] = { 0 }; // dB x 10
        uint32_t delaySyncFlag = 0;
        uint32_t iqSyncFlag = 0;
        uint32_t syncState = 0;
        uint32_t noiseSourceState = 0;
        uint32_t reserved[192] = { 0 };
        uint32_t headerVersion = 7;

        // Payload size implied by this header: cpi_length * active_ant_chs * 8.
        uint64_t payloadBytes() const { return (uint64_t)cpiLength * activeAntChs * 8; }
    };

    // Decode and validate. maxPayloadBytes guards against huge allocations from corrupt headers.
    bool parseHeader(const uint8_t* buf, size_t len, IqHeader& out, std::string* err = nullptr,
                     uint64_t maxPayloadBytes = DEFAULT_MAX_PAYLOAD_BYTES);

    void serializeHeader(const IqHeader& h, uint8_t out[1024]);

    // Sync word ok, DATA frame, delay and IQ sync both reached.
    bool isUsableForDoa(const IqHeader& h);
}
