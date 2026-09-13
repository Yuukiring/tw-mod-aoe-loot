/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MODULE_AOELOOT_H
#define MODULE_AOELOOT_H

#include "ScriptObjects.h"
#include "Config/Config.h"
#include "Player.h"
#include "LootMgr.h"
#include "Creature.h"
#include "Log.h"

#include <vector>
#include <list>
#include <algorithm>
#include <string>

// Maximum loot items count
constexpr size_t MAX_LOOT_ITEMS = 16;

class AOELootPlayer : public PlayerScript
{
public:
    AOELootPlayer() : PlayerScript("AOELootPlayer", { PLAYERHOOK_ON_LOGIN }) {}

    void OnLogin(Player* player) override;
};

class AOELootServer : public ServerScript
{
public:
    AOELootServer() : ServerScript("AOELootServer", { SERVERHOOK_CAN_PACKET_RECEIVE }) {}

    bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override;
};

void AddSC_AoeLoot()
{
    new AOELootPlayer();
    new AOELootServer();
}

#endif // MODULE_AOELOOT_H
