#include"codec.h"
#include<arpa/inet.h>
#include<cstring>

// 打包一帧：4B长度+2B类型+负载
bool Codec::encode(std::uint16_t type, const std::string &payload, std::string &frame)
{
    frame.clear();
    if (payload.size() > MAX_FRAME_LENGTH - TYPE_FIELD_SIZE)
    {
        return false;
    }
    std::uint32_t frame_length = static_cast<std::uint32_t>(TYPE_FIELD_SIZE + payload.size());
    std::uint32_t network_length = htonl(frame_length);
    std::uint16_t network_type = htons(type);
    frame.reserve(LENGTH_FIELD_SIZE + frame_length);
    frame.append(reinterpret_cast<const char *>(&network_length), sizeof(network_length));
    frame.append(reinterpret_cast<const char *>(&network_type), sizeof(network_type));
    frame.append(payload);
    return true;
}

// 拆帧：处理粘包/半包，逐帧回调
bool Codec::decode(Buffer &buffer, const FrameCallback &callback)
{
    if (!callback)
    {
        return false;
    }
    while (1)
    {
        if (buffer.readable_bytes() < LENGTH_FIELD_SIZE)
        {
            return true;
        }
        uint32_t net_len;
        std::memcpy(&net_len, buffer.peek(), sizeof(net_len));
        uint32_t frame_len = ntohl(net_len);
        if (frame_len < MIN_FRAME_LENGTH || frame_len > MAX_FRAME_LENGTH)
        {
            return false;
        }
        if (buffer.readable_bytes() < frame_len + LENGTH_FIELD_SIZE)
        {
            return true;
        }
        uint16_t net_type;
        std::memcpy(&net_type, buffer.peek() + LENGTH_FIELD_SIZE, sizeof(net_type));
        net_type = ntohs(net_type);
        std::string net_payload(buffer.peek() + LENGTH_FIELD_SIZE + TYPE_FIELD_SIZE, frame_len - TYPE_FIELD_SIZE);
        buffer.retrieve(frame_len + LENGTH_FIELD_SIZE);
        if (!callback(net_type, net_payload))
        {
            return false;
        }
    }
    return true;
}
