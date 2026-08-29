// Copyright 2026 Rubato Audio. SPDX-License-Identifier: MIT
// Decode an updater WAV through the real bootloader and compare its binary.
// c++ -O2 -I. plaits/tools/verify_firmware_wav.cc \
//   stm_audio_bootloader/qpsk/{demodulator,packet_decoder}.cc -o build/verify-wav
// build/verify-wav firmware.wav firmware.bin
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include "stm_audio_bootloader/qpsk/demodulator.h"
#include "stm_audio_bootloader/qpsk/packet_decoder.h"
using namespace stm_audio_bootloader;

std::vector<uint8_t> Read(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("cannot open input");
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
}
uint32_t U32(const std::vector<uint8_t>& bytes, size_t p) {
  if (p + 4 > bytes.size()) throw std::runtime_error("truncated RIFF chunk");
  return uint32_t(bytes[p]) | uint32_t(bytes[p+1]) << 8 |
      uint32_t(bytes[p+2]) << 16 | uint32_t(bytes[p+3]) << 24;
}
bool Tag(const std::vector<uint8_t>& bytes, size_t p, const char* tag) {
  return p + 4 <= bytes.size() && std::equal(bytes.begin()+p, bytes.begin()+p+4, tag);
}
int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::runtime_error("usage: verify-wav firmware.wav firmware.bin");
    const auto wav = Read(argv[1]), bin = Read(argv[2]);
    if (!Tag(wav, 0, "RIFF") || !Tag(wav, 8, "WAVE") || bin.empty())
      throw std::runtime_error("invalid WAV or empty binary");
    bool format = false, ended = false;
    size_t start = 0, length = 0;
    for (size_t p = 12; p + 8 <= wav.size();) {
      const size_t size = U32(wav, p+4), data = p+8;
      if (data + size > wav.size()) throw std::runtime_error("truncated WAV data");
      if (Tag(wav, p, "fmt ")) {
        format = size >= 16 && U32(wav, data) == 0x00010001 &&
            U32(wav, data+4) == 48000 && U32(wav, data+12) == 0x00100002;
      }
      if (Tag(wav, p, "data")) { start = data; length = size; }
      p = data + size + (size & 1);
    }
    if (!format || !length || length % 2) throw std::runtime_error("expected mono 48kHz PCM16");
    Demodulator demodulator;
    PacketDecoder decoder;
    decoder.Init(1000, true);
    demodulator.Init(536870912u, 8, 8);
    demodulator.SyncCarrier(true);
    decoder.Reset();
    std::vector<uint8_t> recovered;
    int packets = 0;
    for (size_t i = 8000; i < length / 2 && !ended; ++i) {
      const size_t p = start + i*2;
      const int16_t sample = uint16_t(wav[p]) | uint16_t(wav[p+1]) << 8;
      demodulator.PushSample(2048 + (sample >> 4));
      demodulator.ProcessAtLeast(32);
      while (demodulator.available() && !ended) {
        const auto state = decoder.ProcessSymbol(demodulator.NextSymbol());
        if (state == PACKET_DECODER_STATE_OK) {
          recovered.insert(recovered.end(), decoder.packet_data(), decoder.packet_data()+kPacketSize);
          ++packets;
          decoder.Reset();
          if (packets % 8 == 0) demodulator.SyncCarrier(false);
          else demodulator.SyncDecision();
        } else if (state == PACKET_DECODER_STATE_END_OF_TRANSMISSION) ended = true;
        else if (state == PACKET_DECODER_STATE_ERROR_CRC || state == PACKET_DECODER_STATE_ERROR_SYNC)
          throw std::runtime_error("bootloader CRC/sync error at packet " + std::to_string(packets+1));
      }
    }
    if (!ended || recovered.size() < bin.size() || !std::equal(bin.begin(), bin.end(), recovered.begin()))
      throw std::runtime_error("missing end marker or decoded payload differs from binary");
    for (size_t i = bin.size(); i < recovered.size(); ++i)
      if (recovered[i] != 0xff) throw std::runtime_error("unexpected trailing bytes");
    std::printf("PASS: end marker, %d CRC-checked packets, %zu binary bytes match (+%zu padding).\n",
        packets, bin.size(), recovered.size()-bin.size());
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL: %s\n", error.what());
    return 1;
  }
}
