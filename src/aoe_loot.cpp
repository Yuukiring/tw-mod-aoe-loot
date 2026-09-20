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

#include "aoe_loot.h"
#include "ObjectMgr.h"
#include "Chat/Chat.h"
#include "QuestDef.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Cell.h"
#include "CellImpl.h"

#include <algorithm>
#include <limits>
#include <list>

namespace
{
    constexpr uint32 AOE_LOOT_STACK_LIMIT = 200;

    // Custom grid checker: dead, lootable creatures within range.
    class DeadCreatureInRangeCheck
    {
    public:
        DeadCreatureInRangeCheck(WorldObject const* source, float range)
            : m_source(source), m_range(range) {}

        bool operator()(Creature* creature) const
        {
            if (!creature || creature->IsAlive())
                return false;
            if (!creature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
                return false;
            return creature->IsWithinDistInMap(m_source, m_range, true, SizeFactor::None);
        }

    private:
        WorldObject const* m_source;
        float m_range;
    };

    void GetDeadCreaturesInRange(Player* player, std::list<Creature*>& result, float range)
    {
        DeadCreatureInRangeCheck check(player, range);
        MaNGOS::CreatureListSearcher<DeadCreatureInRangeCheck> searcher(result, check);
        Cell::VisitGridObjects(player, searcher, range);
    }

    bool CanStackRegularLoot(LootItem const& existingItem, LootItem const& incomingItem)
    {
        return !existingItem.is_looted &&
            !incomingItem.is_looted &&
            !existingItem.freeforall &&
            !incomingItem.freeforall &&
            !existingItem.needs_quest &&
            !incomingItem.needs_quest &&
            !existingItem.is_blocked &&
            !incomingItem.is_blocked &&
            existingItem.conditionId == 0 &&
            incomingItem.conditionId == 0 &&
            existingItem.lootOwner.IsEmpty() &&
            incomingItem.lootOwner.IsEmpty() &&
            existingItem.itemid == incomingItem.itemid &&
            existingItem.randomPropertyId == incomingItem.randomPropertyId &&
            existingItem.is_underthreshold == incomingItem.is_underthreshold;
    }

     uint32 AddOrStackRegularLoot(
        Loot* mainLoot,
        LootItem const& incomingItem,
        size_t reservedQuestRows)
    {
        ItemPrototype const* itemProto =
            sObjectMgr.GetItemPrototype(incomingItem.itemid);

        uint32 stackLimit = itemProto
            ? std::min<uint32>(
                itemProto->GetMaxStackSize(),
                AOE_LOOT_STACK_LIMIT)
            : 1;

        bool canStack = stackLimit > 1 &&
            !incomingItem.freeforall &&
            !incomingItem.needs_quest &&
            !incomingItem.is_blocked &&
            incomingItem.conditionId == 0 &&
            incomingItem.lootOwner.IsEmpty();

        uint32 remaining = incomingItem.count;
        uint32 transferred = 0;

        if (canStack)
        {
            for (LootItem& existingItem : mainLoot->items)
            {
                if (!CanStackRegularLoot(existingItem, incomingItem) ||
                    existingItem.count >= stackLimit)
                {
                    continue;
                }

                uint32 amountToAdd = std::min<uint32>(
                    remaining,
                    stackLimit - existingItem.count);

                existingItem.count =
                    static_cast<uint8>(
                        existingItem.count + amountToAdd);

                remaining -= amountToAdd;
                transferred += amountToAdd;

                if (remaining == 0)
                    return transferred;
            }
        }

        while (remaining > 0)
        {
            if (mainLoot->items.size() + reservedQuestRows >=
                MAX_LOOT_ITEMS)
            {
                return transferred;
            }

            LootItem newItem = incomingItem;

            uint32 newStackCount = canStack
                ? std::min<uint32>(remaining, stackLimit)
                : remaining;

            newItem.count = static_cast<uint8>(newStackCount);
            newItem.is_looted = false;
            newItem.is_counted = false;

            mainLoot->items.push_back(newItem);
            transferred += newStackCount;

            if (!newItem.freeforall &&
                newItem.conditionId == 0 &&
                !newItem.needs_quest)
            {
                ++mainLoot->unlootedCount;
            }

            remaining -= newStackCount;

            if (!canStack)
                break;
        }

        return transferred;
    }

    uint8 GetLootSortPriority(LootItem const& item)
    {
        ItemPrototype const* itemProto =
            sObjectMgr.GetItemPrototype(item.itemid);

        // Unknown templates behave like ordinary normal items.
        if (!itemProto)
            return 5;

        // Quality is checked first, regardless of item type.
        switch (itemProto->Quality)
        {
            case ITEM_QUALITY_ARTIFACT:
            case ITEM_QUALITY_LEGENDARY:
                return 0;

            case ITEM_QUALITY_EPIC:
                return 1;

            case ITEM_QUALITY_RARE:
                return 2;

            case ITEM_QUALITY_UNCOMMON:
                return 3;

            case ITEM_QUALITY_POOR:
                return 6;

            case ITEM_QUALITY_NORMAL:
            default:
                break;
        }

        // Normal crafting materials.
        if (itemProto->Class == ITEM_CLASS_TRADE_GOODS ||
            itemProto->Class == ITEM_CLASS_REAGENT ||
            itemProto->Class == ITEM_CLASS_GEM)
        {
            return 4;
        }

        // Normal food and drinks go below grey items.
        if (itemProto->Class == ITEM_CLASS_CONSUMABLE &&
            itemProto->SubClass == ITEM_SUBCLASS_FOOD)
        {
            return 7;
        }

        // Only normal-quality recipes reach this point.
        if (itemProto->Class == ITEM_CLASS_RECIPE)
            return 8;

        return 5;
    }

    bool HasSimpleRegularLootRules(LootItem const& item)
    {
        return !item.freeforall &&
            !item.needs_quest &&
            !item.is_blocked &&
            item.conditionId == 0 &&
            item.lootOwner.IsEmpty();
    }

    bool CanSafelySortLootItem(LootItem const& item)
    {
        return !item.is_looted &&
            HasSimpleRegularLootRules(item);
    }

    // Rolls and master loot point at rows by index.
    bool HasPendingRoll(Loot const& loot)
    {
        auto isPending = [](LootItem const& item)
        {
            return !item.is_looted &&
                (item.is_blocked || !item.lootOwner.IsEmpty());
        };

        // Quest items use is_blocked for per-player visibility tracking
        // (FillQuestLoot), so they are not a pending roll/reservation.
        return std::any_of(
                loot.items.begin(),
                loot.items.end(),
                isPending);
    }

    bool IsReservedByGroupLootRules(
        Player const* player,
        Loot const& loot,
        LootItem const& item)
    {
        Group const* group = player->GetGroup();

        if (!group || group->GetLootMethod() == FREE_FOR_ALL)
            return false;

        // Rolls only start when the corpse is first opened.
        if (group->GetLootMethod() != ROUND_ROBIN &&
            !item.is_underthreshold)
        {
            return true;
        }

        if (!loot.roundRobinPlayer ||
            loot.roundRobinPlayer == player->GetGUID())
        {
            return false;
        }

        return group->GetLootMethod() == ROUND_ROBIN ||
            item.is_underthreshold;
    }

    void CompactTransferredRegularLoot(Loot* loot)
    {
        if (!loot ||
            !std::all_of(
                loot->items.begin(),
                loot->items.end(),
                HasSimpleRegularLootRules))
        {
            return;
        }

        loot->items.erase(
            std::remove_if(
                loot->items.begin(),
                loot->items.end(),
                [](LootItem const& item)
                {
                    return item.is_looted;
                }),
            loot->items.end());
    }

    void SortRegularLoot(Loot* loot)
    {
        if (!loot ||
            !std::all_of(
                loot->items.begin(),
                loot->items.end(),
                CanSafelySortLootItem))
        {
            return;
        }

        std::stable_sort(
            loot->items.begin(),
            loot->items.end(),
            [](LootItem const& left, LootItem const& right)
            {
                return GetLootSortPriority(left) <
                    GetLootSortPriority(right);
            });

    }
}

void AOELootPlayer::OnLogin(Player* player)
{
    if (!player)
        return;

    if (sConfig.GetBoolDefault("AOELoot.Enable", true) &&
        sConfig.GetBoolDefault("AOELoot.Message", true))
    {
        if (WorldSession* session = player->GetSession())
            ChatHandler(session).PSendSysMessage("AOE Loot is enabled.");
    }
}

bool AOELootServer::CanPacketReceive(WorldSession* session, WorldPacket const& packet)
{
    // Only handle loot packets
    if (packet.GetOpcode() != CMSG_LOOT)
        return true;

    // Basic validation checks
    if (!session)
        return true;

    Player* player = session->GetPlayer();
    if (!player)
        return true;

    // Check if module is enabled
    if (!sConfig.GetBoolDefault("AOELoot.Enable", true))
        return true;

    // Check group settings
    if (player->GetGroup() && !sConfig.GetBoolDefault("AOELoot.Group", true))
        return true;

    // Get configured loot range
    float range = sConfig.GetFloatDefault("AOELoot.Range", 55.0f);

    // Limit range to reasonable values
    if (range < 5.0f)
        range = 5.0f;

    if (range > 100.0f)
        range = 100.0f;

    // Read target GUID from packet
    WorldPacket packetCopy(packet);
    ObjectGuid targetGuid;
    packetCopy >> targetGuid;

    if (!targetGuid)
        return true;

    // Get target creature
    Creature* mainCreature = player->GetMap()->GetCreature(targetGuid);
    if (!mainCreature)
        return true;

    // Check if main creature has loot
    if (!mainCreature->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE))
        return true;

    if (HasPendingRoll(mainCreature->loot))
        return true;

    // Get nearby corpses
    std::list<Creature*> nearbyCorpses;
    GetDeadCreaturesInRange(player, nearbyCorpses, range);

    // Remove invalid corpses and main target
    nearbyCorpses.remove_if([&](Creature* c)
        {
            return !c ||
                c->GetObjectGuid() == targetGuid ||
                !c->HasFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE) ||
                c->loot.loot_type == LOOT_SKINNING ||
                HasPendingRoll(c->loot) ||
                !c->IsTappedBy(player);
        });

    // If no other corpses, process normally
    if (nearbyCorpses.empty())
    {
        player->SendLoot(targetGuid, LOOT_CORPSE);
        return false;
    }

    // Get main loot
    Loot* mainLoot = &mainCreature->loot;

    // Track total gold to merge
    uint32 totalGold = mainLoot->gold;

    struct RegularLootCandidate
    {
        Creature* sourceCreature;
        size_t sourceIndex;
        LootItem item;
    };

    struct QuestLootCandidate
    {
        Creature* sourceCreature;
        size_t sourceIndex;
        LootItem item;
    };

    std::vector<Creature*> processedCreatures;
    std::vector<RegularLootCandidate> regularCandidates;
    std::vector<QuestLootCandidate> questCandidates;

    for (Creature* creature : nearbyCorpses)
    {

        if (!creature)
            continue;

        Loot* loot = &creature->loot;

        // Skip already looted corpses.
        if (loot->isLooted())
            continue;

        processedCreatures.push_back(creature);

        // Gold does not consume a loot-window row.
        if (loot->gold > 0)
        {
            if (loot->gold <=
                std::numeric_limits<uint32>::max() - totalGold)
            {
                totalGold += loot->gold;
                loot->gold = 0;
            }
        }

        // Collect safe regular items as candidates. Do not remove
        // anything from the source corpse yet.
        for (size_t i = 0; i < loot->items.size(); ++i)
        {
            LootItem const& item = loot->items[i];

            if (!CanSafelySortLootItem(item))
                continue;

            // The row ends up on the clicked corpse.
            if (!item.AllowedForPlayer(
                    player, mainLoot->GetLootTarget()))
            {
                continue;
            }

            if (IsReservedByGroupLootRules(player, *loot, item) ||
                IsReservedByGroupLootRules(player, *mainLoot, item))
            {
                continue;
            }

            regularCandidates.push_back(
                { creature, i, item });
        }

        // Collect safe quest items needed by the player.
        for (size_t i = 0; i < loot->m_questItems.size(); ++i)
        {
            LootItem const& questItem = loot->m_questItems[i];

            if (!player->HasQuestForItem(questItem.itemid))
                continue;

            // Special group/free-for-all quest items remain on their
            // source corpse so we don't damage ownership information.
            // Note: is_blocked is set by FillQuestLoot during initial loot
            // generation and only tracks whether the row has been added to a
            // player's quest-loot view; it does not indicate ownership, so it
            // must not prevent eligible quest items from being merged.
            if (questItem.freeforall ||
                questItem.conditionId != 0 ||
                !questItem.lootOwner.IsEmpty())
            {
                continue;
            }

            // Quest rows are only filled for looters present at the kill.
            if (!questItem.AllowedForPlayer(
                    player, loot->GetLootTarget()) ||
                !loot->IsAllowedLooter(player->GetObjectGuid()))
            {
                continue;
            }

            uint32 maxNeeded = 0;

            for (auto const& questStatus : player->getQuestStatusMap())
            {
                uint32 questId = questStatus.first;
                QuestStatusData const& status = questStatus.second;

                if (status.m_status == QUEST_STATUS_NONE)
                    continue;

                Quest const* quest =
                    sObjectMgr.GetQuestTemplate(questId);

                if (!quest)
                    continue;

                for (uint8 j = 0;
                     j < QUEST_ITEM_OBJECTIVES_COUNT;
                     ++j)
                {
                    if (quest->ReqItemId[j] ==
                            questItem.itemid &&
                        quest->ReqItemCount[j] >
                            maxNeeded)
                    {
                        maxNeeded =
                            quest->ReqItemCount[j];
                    }
                }
            }

            if (maxNeeded == 0)
                continue;

            uint32 ownedCount =
                player->GetItemCount(questItem.itemid, true);

            for (QuestLootCandidate const& pending :
                 questCandidates)
            {
                if (pending.item.itemid == questItem.itemid)
                    ownedCount += pending.item.count;
            }

            for (LootItem const& mainQuestItem :
                 mainLoot->m_questItems)
            {
                if (mainQuestItem.itemid == questItem.itemid)
                    ownedCount += mainQuestItem.count;
            }

            if (ownedCount >= maxNeeded)
                continue;

            uint32 stillNeeded = maxNeeded - ownedCount;

            LootItem cappedItem = questItem;
            cappedItem.count = std::min(
                static_cast<uint32>(questItem.count),
                stillNeeded);

            questCandidates.push_back(
                { creature, i, cappedItem });
        }
    }

    // Prioritize every regular candidate before consuming rows.
    std::stable_sort(
        regularCandidates.begin(),
        regularCandidates.end(),
        [](RegularLootCandidate const& left,
           RegularLootCandidate const& right)
        {
            return GetLootSortPriority(left.item) <
                GetLootSortPriority(right.item);
        });

    // Merge safe quest items into the main loot window. Quest items
    // that do not fit in the 16-slot client loot window are left on
    // their source corpses.
    for (QuestLootCandidate const& candidate : questCandidates)
    {
        if (!player->HasQuestForItem(candidate.item.itemid))
            continue;

        Loot* sourceLoot = &candidate.sourceCreature->loot;

        if (candidate.sourceIndex >= sourceLoot->m_questItems.size())
            continue;

        LootItem& sourceItem = sourceLoot->m_questItems[candidate.sourceIndex];

        if (sourceItem.is_looted)
            continue;

        size_t existingMainQuestRows = std::count_if(
            mainLoot->m_questItems.begin(),
            mainLoot->m_questItems.end(),
            [](LootItem const& item) { return !item.is_looted; });

        if (mainLoot->items.size() + existingMainQuestRows >= MAX_LOOT_ITEMS)
            break;

        if (mainLoot->m_questItems.size() >= MAX_NR_QUEST_ITEMS)
            break;

        uint32 amountToAdd = std::min<uint32>(
            candidate.item.count,
            sourceItem.count);

        if (amountToAdd == 0)
            continue;

        LootItem newItem = sourceItem;
        newItem.count = static_cast<uint8>(amountToAdd);
        newItem.is_looted = false;
        newItem.is_blocked = false;
        newItem.is_counted = false;
        newItem.lootOwner = ObjectGuid();

        mainLoot->m_questItems.push_back(newItem);

        if (amountToAdd >= sourceItem.count)
        {
            sourceItem.is_looted = true;

            if (sourceLoot->unlootedCount > 0)
                --sourceLoot->unlootedCount;
        }
        else
        {
            sourceItem.count = static_cast<uint8>(
                sourceItem.count - amountToAdd);
        }
    }

    // Refresh the player's quest-loot view so the merged quest items
    // appear in the combined loot window.
    {
        uint32 playerGuidLow = player->GetGUIDLow();
        auto qmapItr = mainLoot->m_playerQuestItems.find(playerGuidLow);
        if (qmapItr != mainLoot->m_playerQuestItems.end())
        {
            delete qmapItr->second;
            mainLoot->m_playerQuestItems.erase(qmapItr);
        }

        mainLoot->FillNotNormalLootFor(player);
    }

    // Quest rows already belonging to the selected corpse, plus any
    // quest rows merged from nearby corpses, occupy loot-window capacity.
    size_t reservedQuestRows = mainLoot->m_questItems.size();

    // Transfer regular candidates in priority order.
    for (RegularLootCandidate const& candidate :
         regularCandidates)
    {
        Loot* sourceLoot =
            &candidate.sourceCreature->loot;

        if (candidate.sourceIndex >=
            sourceLoot->items.size())
        {
            continue;
        }

        LootItem& sourceItem =
            sourceLoot->items[candidate.sourceIndex];

        if (sourceItem.is_looted)
            continue;

        uint32 sourceCount = sourceItem.count;

        uint32 transferred = AddOrStackRegularLoot(
            mainLoot,
            sourceItem,
            reservedQuestRows);

        // Nothing fit. Leave the complete item on its corpse.
        if (transferred == 0)
            continue;

        if (transferred >= sourceCount)
        {
            // The complete source row was transferred.
            sourceItem.is_looted = true;

            if (sourceLoot->unlootedCount > 0)
                --sourceLoot->unlootedCount;
        }
        else
        {
            // Only part of the stack fitted. Preserve the remainder.
            sourceItem.count = static_cast<uint8>(
                sourceCount - transferred);
        }
    }

    // Apply all successfully collected gold to the selected corpse.
    mainLoot->gold = totalGold;

    // Remove only rows that were successfully transferred.
    // Rejected overflow rows remain on their source corpses.
    for (Creature* creature : processedCreatures)
    {
        Loot* loot = &creature->loot;

        CompactTransferredRegularLoot(loot);

        if (!loot->isLooted())
            continue;

        creature->AllLootRemovedFromCorpse();
        creature->RemoveFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
        loot->clear();
    }

    // Organize regular loot before sending the window.
    SortRegularLoot(mainLoot);

    // Send merged loot window
    player->SendLoot(targetGuid, LOOT_CORPSE);

    return false;
}


