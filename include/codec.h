#ifndef CODEC_H
#define CODEC_H

#include "buffer.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

class Codec
{
public:
    using FrameCallback = std::function<bool(std::uint16_t, const std::string &)>;
    static bool encode(std::uint16_t type, const std::string &payload, std::string &frame);
    static bool decode(Buffer &buffer, const FrameCallback &callback);
private:
    static constexpr std::size_t LENGTH_FIELD_SIZE = sizeof(std::uint32_t);
    static constexpr std::size_t TYPE_FIELD_SIZE = sizeof(std::uint16_t);
    static constexpr std::uint32_t MIN_FRAME_LENGTH = 2;
    static constexpr std::uint32_t MAX_FRAME_LENGTH = 64 * 1024;
};

#endif
