/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#ifndef CFBG_APPEARANCE_H
#define CFBG_APPEARANCE_H
#include "WorldPacket.h"
#include <array>

struct CFBGVisualData
{
    uint64 player = 0;
    uint64 guild = 0;
    uint32 model = 0;
    uint8 race = 0;
    uint8 playerClass = 0;
    uint8 gender = 0;
    std::array<uint32, 11> equipment{};
};

// The 5.4.8 mirror-image response supplies race-specific textures independently
// of UNIT_FIELD_SEX and the character's persisted customization fields.
inline WorldPacket BuildCFBGAppearancePacket(CFBGVisualData const& visual)
{
    ObjectGuid guid(visual.player);
    ObjectGuid guild(visual.guild);
    WorldPacket data(SMSG_MIRROR_IMAGE_COMPONENTED_DATA, 100);
    data.WriteGuidMask(guid, 4);
    data.WriteGuidMask(guild, 3, 6);
    data.WriteGuidMask(guid, 0);
    data.WriteGuidMask(guild, 7);
    data.WriteGuidMask(guid, 1, 5);
    data.WriteGuidMask(guild, 2, 1);
    data.WriteGuidMask(guid, 7);
    data.WriteGuidMask(guild, 4, 0);
    data.WriteGuidMask(guid, 2);
    data.WriteGuidMask(guild, 5);
    data.WriteGuidMask(guid, 3);
    data.WriteBits(visual.equipment.size(), 22);
    data.WriteGuidMask(guid, 6);
    data.FlushBits();

    data << uint8(0) << visual.model << uint8(0); // hair color, model, facial hair
    data.WriteGuidBytes(guild, 6, 4);
    data.WriteGuidBytes(guid, 7);
    data.WriteGuidBytes(guild, 1);
    data.WriteGuidBytes(guid, 3);
    data << uint8(0); // hair style
    data.WriteGuidBytes(guid, 2, 0);
    data << visual.race << uint8(0); // race and its default skin
    data.WriteGuidBytes(guild, 7);
    for (uint32 display : visual.equipment)
        data << display;
    data.WriteGuidBytes(guid, 4);
    data << visual.playerClass << visual.gender << uint8(0); // default face
    data.WriteGuidBytes(guid, 5);
    data.WriteGuidBytes(guild, 3, 2);
    data.WriteGuidBytes(guid, 1);
    data.WriteGuidBytes(guild, 0, 5);
    data.WriteGuidBytes(guid, 6);
    return data;
}

inline bool ReadCFBGAppearanceRequest(WorldPacket& packet, uint64& player, uint32& model)
{
    if (packet.size() < 5)
        return false;
    packet.rpos(0);
    packet >> model;
    ObjectGuid guid;
    packet.ReadGuidMask(guid, 0, 2, 1, 6, 5, 4, 7, 3);
    std::size_t bytes = 0;
    for (uint32 index = 0; index < 8; ++index)
        if (guid[index])
            ++bytes;
    if (packet.size() - packet.rpos() != bytes)
        return false;
    packet.ReadGuidBytes(guid, 6, 0, 3, 5, 4, 2, 1, 7);
    player = guid;
    return true;
}
#endif
