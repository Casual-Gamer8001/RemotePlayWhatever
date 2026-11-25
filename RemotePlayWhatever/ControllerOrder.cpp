#include <algorithm>
#include <chrono>
#include <cstring>
#include "ControllerOrder.h"
#include "SteamStuff.h"

static long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

ControllerOrder::ControllerOrder() :
    m_lastGroup(0),
    m_revision(0),
    m_pending(),
    m_clearCb(this, &ControllerOrder::OnClear),
    m_indexSetCb(this, &ControllerOrder::OnIndexSet),
    m_completeCb(this, &ControllerOrder::OnUpdateComplete)
{
}

bool ControllerOrder::CanMove() const
{
    return GClientContext()->ControllerSerialized()->CanMove();
}

std::vector<ControllerSlot> ControllerOrder::Slots(uint32 group) const
{
    std::map<uint32, std::vector<ControllerSlot>>::const_iterator it = m_lists.find(group);
    return it != m_lists.end() ? it->second : std::vector<ControllerSlot>();
}

bool ControllerOrder::SlotInUse(int slot) const
{
    return slot >= 0 && slot < kSlots && GClientContext()->ControllerSerialized()->GetControllerIndexForGamepadIndex(slot) >= 0;
}

bool ControllerOrder::Swap(int slotA, int slotB)
{
    const CControllerSerializedByName* controllers = GClientContext()->ControllerSerialized();
    if (!controllers->CanMove() || slotA == slotB || slotA < 0 || slotB < 0 || slotA >= kSlots || slotB >= kSlots)
    {
        return false;
    }
    // Exactly what Steam's panel does: the swap with the current counter
    // (Steam refuses it if the controllers changed in between), then the
    // Remote Play manager is told, then both controllers rumble.
    const int32 indexA = controllers->GetControllerIndexForGamepadIndex(slotA);
    const int32 indexB = controllers->GetControllerIndexForGamepadIndex(slotB);
    const uint32 counter = controllers->GetGamepadIndexChangeCounter();
    const bool swapped = controllers->BSwapGamepadIndex(counter, slotA, slotB);
    GClientContext()->RemoteClientManager()->OnRemotePlayUIMovedController();
    clientvt::Log("[rp] controller slots %d and %d swapped: %s (counter %u, now %u)\n", slotA, slotB,
                  swapped ? "yes" : "NO", counter, controllers->GetGamepadIndexChangeCounter());
    if (swapped)
    {
        // The swap itself adds 1 to the counter.
        m_pending.active = true;
        m_pending.slotA = slotA;
        m_pending.slotB = slotB;
        m_pending.indexA = indexA;
        m_pending.indexB = indexB;
        m_pending.expected = counter + 1;
        m_pending.at = NowMs();
        CheckPutBack();
    }
    Identify(slotA);
    Identify(slotB);
    if (swapped)
    {
        // Until Steam lists the new order, the lists we have show the swap.
        for (std::map<uint32, std::vector<ControllerSlot>>::iterator it = m_lists.begin(); it != m_lists.end(); ++it)
        {
            for (size_t i = 0; i < it->second.size(); ++i)
            {
                int& slot = it->second[i].slot;
                slot = slot == slotA ? slotB : (slot == slotB ? slotA : slot);
            }
            std::sort(it->second.begin(), it->second.end(),
                      [](const ControllerSlot& a, const ControllerSlot& b) { return a.slot < b.slot; });
        }
    }
    ++m_revision;
    return swapped;
}

void ControllerOrder::CheckPutBack()
{
    if (!m_pending.active)
    {
        return;
    }
    if (NowMs() - m_pending.at > 3000)
    {
        m_pending.active = false;
        return;
    }
    const CControllerSerializedByName* controllers = GClientContext()->ControllerSerialized();
    if (controllers->GetGamepadIndexChangeCounter() == m_pending.expected)
    {
        return;     // nothing else has moved a controller since the swap (yet)
    }
    m_pending.active = false;
    const int slotA = m_pending.slotA;
    const int slotB = m_pending.slotB;
    if (m_pending.indexA == m_pending.indexB ||
        controllers->GetControllerIndexForGamepadIndex(slotA) != m_pending.indexA ||
        controllers->GetControllerIndexForGamepadIndex(slotB) != m_pending.indexB)
    {
        return;
    }
    // Steam put both controllers back where they were (it does that with the
    // first move after a guest joins: the counter goes up by 2), so move them
    // again, once, as a second click would.
    const uint32 counter = controllers->GetGamepadIndexChangeCounter();
    const bool swapped = controllers->BSwapGamepadIndex(counter, slotA, slotB);
    GClientContext()->RemoteClientManager()->OnRemotePlayUIMovedController();
    ++m_revision;
    clientvt::Log("[rp] Steam put controller slots %d and %d back; swapped them again: %s (counter %u, now %u)\n",
                  slotA, slotB, swapped ? "yes" : "NO", counter, controllers->GetGamepadIndexChangeCounter());
}

void ControllerOrder::Identify(int slot) const
{
    const CControllerSerializedByName* controllers = GClientContext()->ControllerSerialized();
    if (slot < 0 || slot >= kSlots || !controllers->Ok())
    {
        return;
    }
    const int32 index = controllers->GetControllerIndexForGamepadIndex(slot);
    if (index >= 0)
    {
        controllers->IdentifyControllerRumbleEffect(index);
    }
}

std::string ControllerOrder::TypeName(int32 type)
{
    // Steam's EControllerType (the same numbers as SDL's controller_type.h,
    // which comes from Valve and must never be renumbered).
    switch (type)
    {
    case 1: case 2: case 3: case 10: return "Steam Controller";
    case 4:  return "Steam Deck";
    case 31: return "Xbox 360 controller";
    case 32: return "Xbox One controller";
    case 46: return "Xbox Elite controller";
    case 33: return "PS3 controller";
    case 34: case 47: return "PS4 controller";
    case 45: return "PS5 controller";
    case 48: return "DualSense Edge";
    case 38: case 44: return "Switch Pro controller";
    case 39: return "Joy-Con (L)";
    case 40: return "Joy-Con (R)";
    case 41: return "Joy-Con pair";
    case 42: return "Switch controller";
    case 51: return "Switch 2 Pro controller";
    case 52: return "Switch 2 controller";
    case 43: return "touch controls";
    case 35: return "Wii controller";
    case 36: return "Apple controller";
    case 37: return "Android controller";
    case 49: return "HORI Steam controller";
    case 50: return "8BitDo controller";
    default: return "controller";
    }
}

void ControllerOrder::OnClear(RemoteClientRemotePlayClearControllers_t* p)
{
    m_building[p->m_unGroupID].clear();
}

void ControllerOrder::OnIndexSet(RemoteClientRemotePlayControllerIndexSet_t* p)
{
    static_assert(sizeof(RemotePlayPlayer_t) == 36, "the player in this callback is 36 bytes");
    uint32 group = 0;
    uint32 slot = 0;
    ControllerSlot entry;
    std::memcpy(&group, p->m_data, 4);
    std::memcpy(&slot, p->m_data + 4, 4);
    std::memcpy(&entry.player, p->m_data + 8, sizeof(entry.player));
    std::memcpy(&entry.type, p->m_data + 44, 4);
    clientvt::Log("[rp] controller slot %u (group %u): steamid %llu guest %u, controller type %d\n", slot, group,
                  (unsigned long long)entry.player.m_playerID.ConvertToUint64(), entry.player.m_unGuestID, entry.type);
    if (slot >= (uint32)kSlots)
    {
        return;
    }
    entry.slot = (int)slot;
    std::vector<ControllerSlot>& list = m_building[group];
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (list[i].slot == entry.slot)
        {
            list[i] = entry;
            return;
        }
    }
    list.push_back(entry);
}

void ControllerOrder::OnUpdateComplete(RemoteClientRemotePlayUpdateComplete_t* p)
{
    std::vector<ControllerSlot> list = m_building[p->m_unGroupID];
    std::sort(list.begin(), list.end(), [](const ControllerSlot& a, const ControllerSlot& b) { return a.slot < b.slot; });
    m_lists[p->m_unGroupID] = list;
    m_lastGroup = p->m_unGroupID;
    ++m_revision;
    clientvt::Log("[rp] controller list for group %u complete: %zu controller(s)\n", p->m_unGroupID, list.size());
    CheckPutBack();
}
