#ifndef CHECKPOINT_CODEC_H
#define CHECKPOINT_CODEC_H

#include "checkpoint.h"
#include <cstddef>
#include <string>

class CheckpointCodec
{
public:
    enum class States
    {
        success,
        invalid_checkpoint,
        malformed_data,
        unsupported_version,
        checksum_mismatch,
        too_large
    };
    static constexpr std::size_t MAX_ENCODED_SIZE =
        16 * 1024 * 1024;
    static States encode(const ServerCheckpoint &checkpoint, std::string &data);   // 编码：检查点序列化为二进制数据
    static States decode(const std::string &data, ServerCheckpoint &checkpoint);   // 解码：二进制数据还原为检查点
};

#endif
