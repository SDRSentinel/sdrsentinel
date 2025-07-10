// Heimdall IQ header: byte offsets, round trip, validation, usability rule.
#include <heimdall/iq_header.h>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace kraken_doa::heimdall;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static IqHeader sample() {
    IqHeader h;
    h.frameType = FRAME_CAL;
    std::strcpy(h.hardwareId, "kraken5");
    h.unitId = 0x11223344;
    h.activeAntChs = 5;
    h.iooType = 2;
    h.rfCenterFreq = 433920000ull;
    h.adcSamplingFreq = 2400000ull;
    h.samplingFreq = 1200000ull;
    h.cpiLength = 4096;
    h.timeStamp = 0x0102030405060708ull;
    h.daqBlockIndex = 77;
    h.cpiIndex = 0xA1B2C3D4;
    h.extIntegrationCntr = 0x1122334455667788ull;
    h.dataType = 3;
    h.sampleBitDepth = 32;
    h.adcOverdriveFlags = 0x15;
    for (uint32_t i = 0; i < 32; i++) { h.ifGains[i] = 100 + i; }
    h.delaySyncFlag = 1;
    h.iqSyncFlag = 1;
    h.syncState = 6;
    h.noiseSourceState = 1;
    for (uint32_t i = 0; i < 192; i++) { h.reserved[i] = 0xF0000000u + i; }
    h.headerVersion = 7;
    return h;
}


int main() {
    // --- exact bytes against hand-written values
    {
        IqHeader h;
        h.activeAntChs = 5;
        h.rfCenterFreq = 0x0000000019EC0A80ull; // 434899584
        h.cpiLength = 0x00001000;
        h.cpiIndex = 0xAABBCCDD;
        h.ifGains[0] = 0x01020304;
        h.delaySyncFlag = 1;
        h.iqSyncFlag = 1;
        h.headerVersion = 7;
        uint8_t b[1024];
        serializeHeader(h, b);
        CHECK(b[0] == 0x5a && b[1] == 0xb9 && b[2] == 0xf7 && b[3] == 0x2b);       // sync_word 0x2bf7b95a LE
        CHECK(b[28] == 5 && b[29] == 0 && b[30] == 0 && b[31] == 0);                // active_ant_chs
        const uint8_t freq[8] = { 0x80, 0x0a, 0xec, 0x19, 0, 0, 0, 0 };
        CHECK(std::memcmp(b + 40, freq, 8) == 0);                                   // rf_center_freq
        CHECK(b[64] == 0x00 && b[65] == 0x10 && b[66] == 0 && b[67] == 0);          // cpi_length
        CHECK(b[84] == 0xDD && b[85] == 0xCC && b[86] == 0xBB && b[87] == 0xAA);    // cpi_index
        CHECK(b[108] == 0x04 && b[109] == 0x03 && b[110] == 0x02 && b[111] == 0x01); // if_gains[0]
        CHECK(b[236] == 1 && b[237] == 0);                                          // delay_sync_flag
        CHECK(b[240] == 1 && b[241] == 0);                                          // iq_sync_flag
        CHECK(b[1020] == 7 && b[1021] == 0 && b[1022] == 0 && b[1023] == 0);        // header_version
        CHECK(b[36] == 0 && b[68] == 0);                                            // padding stays zero

        // parse hand-built bytes (not produced by serializeHeader)
        uint8_t hb[1024] = { 0 };
        const uint8_t sync[4] = { 0x5a, 0xb9, 0xf7, 0x2b };
        std::memcpy(hb, sync, 4);
        hb[28] = 5;
        std::memcpy(hb + 40, freq, 8);
        hb[64] = 0x00; hb[65] = 0x10;
        hb[84] = 0xDD; hb[85] = 0xCC; hb[86] = 0xBB; hb[87] = 0xAA;
        hb[100] = 32;
        hb[108] = 0x04; hb[109] = 0x03; hb[110] = 0x02; hb[111] = 0x01;
        hb[236] = 1; hb[240] = 1;
        hb[1020] = 7;
        IqHeader p;
        std::string err;
        CHECK(parseHeader(hb, 1024, p, &err));
        CHECK(p.activeAntChs == 5 && p.rfCenterFreq == 0x19EC0A80ull && p.cpiLength == 4096);
        CHECK(p.cpiIndex == 0xAABBCCDDu && p.ifGains[0] == 0x01020304u);
        CHECK(p.delaySyncFlag == 1 && p.iqSyncFlag == 1 && p.headerVersion == 7);
        CHECK(p.payloadBytes() == 4096ull * 5 * 8);
    }

    // --- full round trip of every field
    {
        IqHeader h = sample();
        uint8_t b[1024];
        serializeHeader(h, b);
        IqHeader p;
        std::string err;
        CHECK(parseHeader(b, 1024, p, &err));
        CHECK(p.syncWord == h.syncWord && p.frameType == h.frameType);
        CHECK(std::strcmp(p.hardwareId, "kraken5") == 0);
        CHECK(p.unitId == h.unitId && p.activeAntChs == 5 && p.iooType == 2);
        CHECK(p.rfCenterFreq == h.rfCenterFreq && p.adcSamplingFreq == h.adcSamplingFreq && p.samplingFreq == h.samplingFreq);
        CHECK(p.cpiLength == 4096 && p.timeStamp == h.timeStamp && p.daqBlockIndex == 77);
        CHECK(p.cpiIndex == h.cpiIndex && p.extIntegrationCntr == h.extIntegrationCntr);
        CHECK(p.dataType == 3 && p.sampleBitDepth == 32 && p.adcOverdriveFlags == 0x15);
        for (int i = 0; i < 32; i++) { CHECK(p.ifGains[i] == h.ifGains[i]); }
        CHECK(p.delaySyncFlag == 1 && p.iqSyncFlag == 1 && p.syncState == 6 && p.noiseSourceState == 1);
        for (int i = 0; i < 192; i++) { CHECK(p.reserved[i] == h.reserved[i]); }
        CHECK(p.headerVersion == 7);
        uint8_t b2[1024];
        serializeHeader(p, b2);
        CHECK(std::memcmp(b, b2, 1024) == 0);
    }

    // --- validation
    {
        IqHeader h = sample(), p;
        uint8_t b[1024];
        std::string err;
        serializeHeader(h, b);
        CHECK(!parseHeader(b, 1023, p, &err));                         // short
        b[0] ^= 1;
        CHECK(!parseHeader(b, 1024, p, &err) && err.find("sync") != std::string::npos); // wrong sync word
        h.activeAntChs = 0; serializeHeader(h, b);
        CHECK(!parseHeader(b, 1024, p, &err));
        h.activeAntChs = 33; serializeHeader(h, b);
        CHECK(!parseHeader(b, 1024, p, &err));
        h.activeAntChs = 32; serializeHeader(h, b);
        CHECK(parseHeader(b, 1024, p, &err));
        h = sample(); h.sampleBitDepth = 16; serializeHeader(h, b);
        CHECK(!parseHeader(b, 1024, p, &err));
        h.cpiLength = 0; serializeHeader(h, b);                       // empty payload: bit depth not checked
        CHECK(parseHeader(b, 1024, p, &err));
        h = sample(); h.cpiLength = 0xFFFFFFFFu; serializeHeader(h, b);
        CHECK(!parseHeader(b, 1024, p, &err) && err.find("limit") != std::string::npos); // oversize payload
        h = sample(); serializeHeader(h, b);
        CHECK(parseHeader(b, 1024, p, &err, 4096ull * 5 * 8));
        CHECK(!parseHeader(b, 1024, p, &err, 4096ull * 5 * 8 - 1));
    }

    // --- usability truth table
    {
        for (uint32_t ft = 0; ft < 5; ft++) {
            for (uint32_t d = 0; d < 2; d++) {
                for (uint32_t q = 0; q < 2; q++) {
                    IqHeader h;
                    h.frameType = ft; h.delaySyncFlag = d; h.iqSyncFlag = q;
                    bool expect = (ft == FRAME_DATA && d == 1 && q == 1);
                    CHECK(isUsableForDoa(h) == expect);
                }
            }
        }
        IqHeader h; h.frameType = FRAME_DATA; h.delaySyncFlag = 1; h.iqSyncFlag = 1;
        CHECK(isUsableForDoa(h));
        h.syncWord = 0; CHECK(!isUsableForDoa(h));
        h = IqHeader(); h.frameType = FRAME_DATA; h.delaySyncFlag = 2; h.iqSyncFlag = 1;
        CHECK(!isUsableForDoa(h));
    }

    std::printf(failures ? "FAILED (%d)\n" : "test_kraken_heimdall_header OK\n", failures);
    return failures ? 1 : 0;
}
