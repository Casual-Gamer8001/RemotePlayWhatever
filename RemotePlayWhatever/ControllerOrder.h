#ifndef CONTROLLERORDER_H
#define CONTROLLERORDER_H

#include <Steamworks.h>
#include <map>
#include <string>
#include <vector>

// Steam's Remote Play manager tells its UI which controller is in each player
// slot with three callbacks, which Open Steamworks doesn't declare: a group's
// list is cleared, each slot is set, then the list is complete. (Found in
// SteamUI.dll of Steam 1794536281 and the 1795567705 beta, whose
// CRemotePlayController registers them with these IDs and sizes.)
struct RemoteClientRemotePlayClearControllers_t
{
    enum { k_iCallback = k_iClientRemoteClientManagerCallbacks + 35 };
    uint32 m_unGroupID;
};

// 48 bytes: the group (4), the slot (4), whose controller it is (a
// RemotePlayPlayer_t, 36) and Steam's kind of controller (4, an
// EControllerType). Read by offset, so packing can't move anything.
struct RemoteClientRemotePlayControllerIndexSet_t
{
    enum { k_iCallback = k_iClientRemoteClientManagerCallbacks + 36 };
    unsigned char m_data[48];
};

struct RemoteClientRemotePlayUpdateComplete_t
{
    enum { k_iCallback = k_iClientRemoteClientManagerCallbacks + 38 };
    uint32 m_unGroupID;
};

// The controller in one player slot; slot 0 is player 1.
struct ControllerSlot
{
    int slot;
    RemotePlayPlayer_t player;  // whose: the host (guest ID 0) or a guest
    int32 type;                 // Steam's EControllerType (0: unknown)
};

// Which controller each player of a Remote Play Together session has, and
// moving them to other players, the way Steam's own Remote Play Together
// panel does. Games take controllers in slot order, so moving a guest's
// controller to slot 0 makes them player 1.
class ControllerOrder
{
public:
    // Steam has 20 slots. Games that take XInput controllers see only the
    // first 4 (Steam's panel calls those the priority slots); others see
    // later ones too (Ryujinx, which reads Steam's virtual controllers through
    // SDL's raw input, took a controller in slot 5).
    static const int kSlots = 20;
    static const int kPlayerSlots = 4;

    ControllerOrder();

    // Whether this Steam lets the app move controllers (Windows only for now:
    // see ClientContext::ControllerSerialized).
    bool CanMove() const;
    // The controllers Steam last listed for a group, in slot order, and the
    // group it listed last (0: none yet).
    std::vector<ControllerSlot> Slots(uint32 group) const;
    bool HasList(uint32 group) const { return m_lists.count(group) != 0; }
    uint32 LastGroup() const { return m_lastGroup; }
    // Whether a slot has a controller right now (asks Steam; works without a list).
    bool SlotInUse(int slot) const;
    // Bumped whenever Steam sends a new list or a controller is moved.
    uint32 Revision() const { return m_revision; }

    // Swap the controllers of two slots (either may be empty), like dragging
    // a player onto another in Steam's panel (without its rumble).
    bool Swap(int slotA, int slotB);
    // Rumble the controller in a slot, so its owner knows which it is.
    void Identify(int slot) const;

    // "Xbox 360 controller", "touch controls"... ("controller" if unknown).
    static std::string TypeName(int32 type);

private:
    // The last swap, in case Steam puts it back (it does with the first move
    // after a guest joins): checked when Steam's change counter moves on.
    struct PendingSwap
    {
        bool active;
        int slotA;
        int slotB;
        int32 indexA;       // the controller in slotA before the swap (-1: none)
        int32 indexB;
        uint32 expected;    // the change counter right after the swap
        long long at;       // when it was made (ms)
    };
    // Swaps again, once, if Steam put the pending swap back within 3 s.
    void CheckPutBack();

    std::map<uint32, std::vector<ControllerSlot>> m_building;
    std::map<uint32, std::vector<ControllerSlot>> m_lists;
    uint32 m_lastGroup;
    uint32 m_revision;
    PendingSwap m_pending;

    STEAM_CALLBACK(ControllerOrder, OnClear, RemoteClientRemotePlayClearControllers_t, m_clearCb);
    STEAM_CALLBACK(ControllerOrder, OnIndexSet, RemoteClientRemotePlayControllerIndexSet_t, m_indexSetCb);
    STEAM_CALLBACK(ControllerOrder, OnUpdateComplete, RemoteClientRemotePlayUpdateComplete_t, m_completeCb);
};

#endif // CONTROLLERORDER_H
