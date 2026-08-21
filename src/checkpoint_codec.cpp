#include "checkpoint_codec.h"
#include<cstddef>
#include<cstdint>
#include<cstring>
#include<limits>
#include<new>
#include<set>
#include<stdexcept>
#include<string>
#include<utility>

namespace
{
    constexpr std::size_t CHECKPOINT_HEADER_SIZE = 40;
    constexpr std::size_t MAX_CHECKPOINT_FILE_SIZE = 64U * 1024U * 1024U;
    constexpr std::size_t MAX_PAYLOAD_SIZE = MAX_CHECKPOINT_FILE_SIZE - CHECKPOINT_HEADER_SIZE;
    constexpr std::uint32_t CHECKPOINT_VERSION = 1;
    constexpr char CHECKPOINT_MAGIC[4] =
    {
        'L', 'W', 'C', 'P'
    };
    constexpr std::uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
    constexpr std::uint64_t FNV_PRIME = 1099511628211ULL;
    constexpr std::size_t MIN_ROOM_RECORD_SIZE = 37;
    constexpr std::size_t MIN_MEMBER_RECORD_SIZE = 12;
    constexpr std::size_t GAME_STATE_RECORD_SIZE = 20;
    constexpr std::size_t MIN_SESSION_RECORD_SIZE = 16;
    constexpr std::size_t SESSION_TOKEN_SIZE = 32;
    // 判断：令牌是否为 32 位小写十六进制
    bool valid_session_token(const std::string &token)
    {
        if (token.size() != SESSION_TOKEN_SIZE)
        {
            return false;
        }
        for (char ch : token)
        {
            const bool is_lower_hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
            if (!is_lower_hex)
            {
                return false;
            }
        }
        return true;
    }
    // 校验：房间号与令牌全局唯一，会话令牌合法
    bool valid_checkpoint_keys(const ServerCheckpoint &checkpoint)
    {
        std::set<std::uint32_t> room_ids;
        for (const RoomCheckpoint &room : checkpoint.rooms)
        {
            if (!room_ids.insert(room.room_id).second)
            {
                return false;
            }
        }
        std::set<std::string> session_tokens;
        std::set<std::pair<std::uint32_t, std::uint64_t>>session_identities;
        for (const SessionCheckpoint &session : checkpoint.sessions)
        {
            if (!valid_session_token(session.token) || !session_tokens.insert(session.token).second || !session_identities.insert({session.room_id, session.player_id}).second)
            {
                return false;
            }
        }
        return true;
    }
    // 判断：追加 size 字节后是否不超过 limit
    bool can_append(const std::string &data, std::size_t size, std::size_t limit)
    {
        return size <= limit && data.size() <= limit - size;
    }
    // 追加：原始字节块（超限返回 false）
    bool append_bytes(std::string &data, const char *source, std::size_t size, std::size_t limit)
    {
        if (!can_append(data, size, limit))
        {
            return false;
        }
        data.append(source, size);
        return true;
    }
    // 追加：1 字节
    bool append_u8(std::string &data, std::uint8_t value, std::size_t limit)
    {
        if (!can_append(data, 1, limit))
        {
            return false;
        }
        data.push_back(static_cast<char>(value));
        return true;
    }
    // 追加：4 字节大端
    bool append_u32(std::string &data, std::uint32_t value, std::size_t limit)
    {
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            const std::uint8_t byte = static_cast<std::uint8_t>((value >> shift) & 0xffU);
            if (!append_u8(data, byte, limit))
            {
                return false;
            }
        }
        return true;
    }
    // 追加：8 字节大端
    bool append_u64(std::string &data, std::uint64_t value, std::size_t limit)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            const std::uint8_t byte = static_cast<std::uint8_t>((value >> shift) & 0xffULL);
            if (!append_u8(data, byte, limit))
            {
                return false;
            }
        }
        return true;
    }
    // 判断：value 能否用 uint32 表示
    bool fits_u32(std::size_t value)
    {
        return value <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max());
    }
    // 映射：房间状态 → 1 字节
    bool room_state_to_byte(Roomstatemachine::States state, std::uint8_t &value)
    {
        switch (state)
        {
            case Roomstatemachine::States::waiting:
                value = 0;
                return true;
            case Roomstatemachine::States::running:
                value = 1;
                return true;
            case Roomstatemachine::States::finished:
                value = 2;
                return true;
        }
        return false;
    }
    // 映射：1 字节 → 房间状态
    bool byte_to_room_state(std::uint8_t value, Roomstatemachine::States &state)
    {
        switch (value)
        {
            case 0:
                state = Roomstatemachine::States::waiting;
                return true;
            case 1:
                state = Roomstatemachine::States::running;
                return true;
            case 2:
                state = Roomstatemachine::States::finished;
                return true;
            default:
                return false;
        }
    }
    // 转换：int32 按位转为 uint32（不改位模式）
    std::uint32_t int32_to_bits(std::int32_t value)
    {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
    // 转换：uint32 按位还原 int32
    std::int32_t bits_to_int32(std::uint32_t bits)
    {
        std::int32_t value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }
    // 计算：FNV-1a 校验和
    std::uint64_t checksum(const std::string &data, std::size_t begin, std::size_t size)
    {
        std::uint64_t hash = FNV_OFFSET_BASIS;
        for (std::size_t i = 0; i < size; ++i)
        {
            const unsigned char byte = static_cast<unsigned char>(data[begin + i]);
            hash ^= static_cast<std::uint64_t>(byte);
            hash *= FNV_PRIME;
        }
        return hash;
    }
    class Reader
    {
    public:
        // 构造：从 data 的 [begin, end) 区间顺序读取
        Reader(const std::string &data, std::size_t begin, std::size_t end) : data_(data), position_(begin), end_(end)
        {
        }
        // 查询：剩余未读字节数
        std::size_t remaining() const
        {
            return end_ - position_;
        }
        // 判断：是否已读完
        bool finished() const
        {
            return position_ == end_;
        }
        // 读取：1 字节
        bool read_u8(std::uint8_t &value)
        {
            if (remaining() < 1)
            {
                return false;
            }
            value = static_cast<std::uint8_t>(static_cast<unsigned char>(data_[position_]));
            ++position_;
            return true;
        }
        // 读取：4 字节大端
        bool read_u32(std::uint32_t &value)
        {
            if (remaining() < 4)
            {
                return false;
            }
            value = 0;
            for (int i = 0; i < 4; ++i)
            {
                value <<= 8;
                value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data_[position_]));
                ++position_;
            }
            return true;
        }
        // 读取：8 字节大端
        bool read_u64(std::uint64_t &value)
        {
            if (remaining() < 8)
            {
                return false;
            }
            value = 0;
            for (int i = 0; i < 8; ++i)
            {
                value <<= 8;
                value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data_[position_]));
                ++position_;
            }
            return true;
        }
        // 读取：定长字符串
        bool read_string(std::uint32_t size, std::string &value)
        {
            const std::size_t string_size = static_cast<std::size_t>(size);
            if (remaining() < string_size)
            {
                return false;
            }
            value.assign(data_.data() + position_, string_size);
            position_ += string_size;
            return true;
        }
    private:
        const std::string &data_;
        std::size_t position_;
        std::size_t end_;
    };

}

// 编码：检查点序列化为二进制数据（魔法+版本+代号+校验和）
CheckpointCodec::States CheckpointCodec::encode(const ServerCheckpoint &checkpoint, std::string &data)
{
    try
    {
        if (checkpoint.generation == 0)
        {
            return States::invalid_checkpoint;
        }
        if (!fits_u32(checkpoint.rooms.size()) || !fits_u32(checkpoint.sessions.size()))
        {
            return States::too_large;
        }
        if (!valid_checkpoint_keys(checkpoint))
        {
            return States::invalid_checkpoint;
        }
        std::string payload;
        for (const RoomCheckpoint &room : checkpoint.rooms)
        {
            if (!fits_u32(room.members.size()) || !fits_u32(room.game_states.size()))
            {
                return States::too_large;
            }
            std::uint8_t room_state = 0;
            if (!room_state_to_byte(room.state, room_state))
            {
                return States::invalid_checkpoint;
            }
            if (!append_u32(payload, room.room_id, MAX_PAYLOAD_SIZE) || !append_u64(payload, room.capacity, MAX_PAYLOAD_SIZE) || !append_u64(payload, room.next_player_id, MAX_PAYLOAD_SIZE) || !append_u8(payload, room_state, MAX_PAYLOAD_SIZE) || !append_u64(payload, room.tick_id, MAX_PAYLOAD_SIZE) || !append_u32(payload, static_cast<std::uint32_t>(room.members.size()), MAX_PAYLOAD_SIZE))
            {
                return States::too_large;
            }
            for (const CheckpointMember &member : room.members)
            {
                if (!fits_u32(member.player_name.size()))
                {
                    return States::too_large;
                }
                if (!append_u64(payload, member.player_id, MAX_PAYLOAD_SIZE) || !append_u32(payload, static_cast<std::uint32_t>(member.player_name.size()), MAX_PAYLOAD_SIZE) || !append_bytes(payload, member.player_name.data(), member.player_name.size(), MAX_PAYLOAD_SIZE))
                {
                    return States::too_large;
                }
            }
            if (!append_u32(payload, static_cast<std::uint32_t>(room.game_states.size()), MAX_PAYLOAD_SIZE))
            {
                return States::too_large;
            }
            for (const PlayerGameState &player : room.game_states)
            {
                if (!append_u64(payload, player.player_id, MAX_PAYLOAD_SIZE) || !append_u32(payload, int32_to_bits(player.x), MAX_PAYLOAD_SIZE) || !append_u32(payload, int32_to_bits(player.y), MAX_PAYLOAD_SIZE) || !append_u32(payload, int32_to_bits(player.hp), MAX_PAYLOAD_SIZE))
                {
                    return States::too_large;
                }
            }
        }
        for (const SessionCheckpoint &session : checkpoint.sessions)
        {
            if (!fits_u32(session.token.size()))
            {
                return States::too_large;
            }
            if (!append_u32(payload, static_cast<std::uint32_t>(session.token.size()), MAX_PAYLOAD_SIZE) || !append_bytes(payload, session.token.data(), session.token.size(), MAX_PAYLOAD_SIZE) || !append_u32(payload, session.room_id, MAX_PAYLOAD_SIZE) || !append_u64(payload, session.player_id, MAX_PAYLOAD_SIZE))
            {
                return States::too_large;
            }
        }
        const std::uint64_t payload_checksum = checksum(payload, 0, payload.size());
        std::string result;
        result.reserve(CHECKPOINT_HEADER_SIZE + payload.size());
        if (!append_bytes(result, CHECKPOINT_MAGIC, sizeof(CHECKPOINT_MAGIC), MAX_CHECKPOINT_FILE_SIZE) || !append_u32(result, CHECKPOINT_VERSION, MAX_CHECKPOINT_FILE_SIZE) || !append_u64(result, checkpoint.generation, MAX_CHECKPOINT_FILE_SIZE) || !append_u32(result, static_cast<std::uint32_t>(checkpoint.rooms.size()), MAX_CHECKPOINT_FILE_SIZE) || !append_u32(result, static_cast<std::uint32_t>(checkpoint.sessions.size()), MAX_CHECKPOINT_FILE_SIZE) || !append_u64(result, static_cast<std::uint64_t>(payload.size()), MAX_CHECKPOINT_FILE_SIZE) || !append_u64(result, payload_checksum, MAX_CHECKPOINT_FILE_SIZE) || !append_bytes(result, payload.data(), payload.size(), MAX_CHECKPOINT_FILE_SIZE))
        {
            return States::too_large;
        }
        data = std::move(result);
        return States::success;
    }
    catch (const std::bad_alloc &)
    {
        return States::too_large;
    }
    catch (const std::length_error &)
    {
        return States::too_large;
    }
}

// 解码：二进制数据还原为检查点（校验魔法/版本/校验和）
CheckpointCodec::States CheckpointCodec::decode(const std::string &data, ServerCheckpoint &checkpoint)
{
    try
    {
        if (data.size() > MAX_CHECKPOINT_FILE_SIZE)
        {
            return States::too_large;
        }
        if (data.size() < CHECKPOINT_HEADER_SIZE)
        {
            return States::malformed_data;
        }
        if (data.compare(0, sizeof(CHECKPOINT_MAGIC), CHECKPOINT_MAGIC, sizeof(CHECKPOINT_MAGIC)) != 0)
        {
            return States::malformed_data;
        }
        Reader header(data, sizeof(CHECKPOINT_MAGIC), CHECKPOINT_HEADER_SIZE);
        std::uint32_t version = 0;
        std::uint64_t generation = 0;
        std::uint32_t room_count = 0;
        std::uint32_t session_count = 0;
        std::uint64_t payload_size = 0;
        std::uint64_t stored_checksum = 0;
        if (!header.read_u32(version) || !header.read_u64(generation) || !header.read_u32(room_count) || !header.read_u32(session_count) || !header.read_u64(payload_size) || !header.read_u64(stored_checksum) || !header.finished())
        {
            return States::malformed_data;
        }
        if (version != CHECKPOINT_VERSION)
        {
            return States::unsupported_version;
        }
        if (generation == 0)
        {
            return States::invalid_checkpoint;
        }
        const std::size_t actual_payload_size = data.size() - CHECKPOINT_HEADER_SIZE;
        if (payload_size != static_cast<std::uint64_t>(actual_payload_size))
        {
            return States::malformed_data;
        }
        const std::uint64_t actual_checksum = checksum(data, CHECKPOINT_HEADER_SIZE, actual_payload_size);
        if (stored_checksum != actual_checksum)
        {
            return States::checksum_mismatch;
        }
        Reader reader(data, CHECKPOINT_HEADER_SIZE, data.size());
        if (static_cast<std::size_t>(room_count) > reader.remaining() / MIN_ROOM_RECORD_SIZE)
        {
            return States::malformed_data;
        }
        ServerCheckpoint temp;
        temp.generation = generation;
        temp.rooms.reserve(room_count);
        for (std::uint32_t i = 0; i < room_count; ++i)
        {
            RoomCheckpoint room;
            std::uint8_t state_value = 0;
            std::uint32_t member_count = 0;
            if (!reader.read_u32(room.room_id) || !reader.read_u64(room.capacity) || !reader.read_u64(room.next_player_id) || !reader.read_u8(state_value) || !reader.read_u64(room.tick_id) || !reader.read_u32(member_count))
            {
                return States::malformed_data;
            }
            if (!byte_to_room_state(state_value, room.state))
            {
                return States::malformed_data;
            }
            if (reader.remaining() < 4)
            {
                return States::malformed_data;
            }
            if (static_cast<std::size_t>(member_count) > (reader.remaining() - 4) / MIN_MEMBER_RECORD_SIZE)
            {
                return States::malformed_data;
            }
            room.members.reserve(member_count);
            for (std::uint32_t j = 0; j < member_count; ++j)
            {
                CheckpointMember member;
                std::uint32_t name_size = 0;
                if (!reader.read_u64(member.player_id) || !reader.read_u32(name_size) || !reader.read_string(name_size, member.player_name))
                {
                    return States::malformed_data;
                }
                room.members.push_back(std::move(member));
            }
            std::uint32_t game_state_count = 0;
            if (!reader.read_u32(game_state_count))
            {
                return States::malformed_data;
            }
            if (static_cast<std::size_t>(game_state_count) > reader.remaining() / GAME_STATE_RECORD_SIZE)
            {
                return States::malformed_data;
            }
            room.game_states.reserve(game_state_count);
            for (std::uint32_t j = 0;j < game_state_count;++j)
            {
                PlayerGameState player{};
                std::uint32_t x_bits = 0;
                std::uint32_t y_bits = 0;
                std::uint32_t hp_bits = 0;
                if (!reader.read_u64(player.player_id) || !reader.read_u32(x_bits) || !reader.read_u32(y_bits) || !reader.read_u32(hp_bits))
                {
                    return States::malformed_data;
                }
                player.x = bits_to_int32(x_bits);
                player.y = bits_to_int32(y_bits);
                player.hp = bits_to_int32(hp_bits);
                room.game_states.push_back(player);
            }
            temp.rooms.push_back(std::move(room));
        }
        if (static_cast<std::size_t>(session_count) > reader.remaining() / MIN_SESSION_RECORD_SIZE)
        {
            return States::malformed_data;
        }
        temp.sessions.reserve(session_count);
        for (std::uint32_t i = 0;i < session_count;++i)
        {
            SessionCheckpoint session;
            std::uint32_t token_size = 0;
            if (!reader.read_u32(token_size) ||
                !reader.read_string(
                token_size,
                session.token) ||
                !reader.read_u32(session.room_id) ||
                !reader.read_u64(session.player_id))
            {
                return States::malformed_data;
            }
            temp.sessions.push_back(std::move(session));
        }
        if (!reader.finished())
        {
            return States::malformed_data;
        }
        if (!valid_checkpoint_keys(temp))
        {
            return States::invalid_checkpoint;
        }
        checkpoint = std::move(temp);
        return States::success;
    }
    catch (const std::bad_alloc &)
    {
        return States::too_large;
    }
    catch (const std::length_error &)
    {
        return States::too_large;
    }
}
