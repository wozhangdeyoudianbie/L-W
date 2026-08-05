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
    using FrameCallback = std::function<bool(std::uint16_t, const std::string &)>;   // 回调返回 false 可中止拆帧
    static bool encode(std::uint16_t type, const std::string &payload, std::string &frame);  // 打包一帧：4B长度+2B类型+负载
    static bool decode(Buffer &buffer, const FrameCallback &callback);                        // 从缓冲区拆出完整帧（处理粘包/半包）
private:
    static constexpr std::size_t LENGTH_FIELD_SIZE = sizeof(std::uint32_t);
    static constexpr std::size_t TYPE_FIELD_SIZE = sizeof(std::uint16_t);
    static constexpr std::uint32_t MIN_FRAME_LENGTH = 2;
    static constexpr std::uint32_t MAX_FRAME_LENGTH = 64 * 1024;
};

#endif
