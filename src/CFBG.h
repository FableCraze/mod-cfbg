/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#ifndef CFBG_H
#define CFBG_H
#include "SharedDefines.h"
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <string>
class Player;
class Group;
class Battleground;
class BattlegroundQueue;
class Battlefield;
class ChatHandler;
class Unit;
struct GroupQueueInfo;
struct CFBGAppearance
{
    uint32 originalModel;
    uint32 model;
    uint8 race;
    TeamId team;
    bool battlefield;
    int32 frostwolfReaction = -1;
    int32 stormpikeReaction = -1;
};
class CFBG
{
public:
    static CFBG& Instance();
    void LoadConfig();
    bool SelectGroups(BattlegroundQueue*, Battleground*, uint8, uint32, uint32, bool);
    bool CanJoin(Player*, Group*);
    bool Announce(BattlegroundQueue*, Battleground*, Player*, uint8, uint32, uint32);
    void BeforeAdd(Battleground*, Player*);
    void AfterAdd(Battleground*, Player*);
    void BalanceAtStart(Battleground*);
    void Apply(Player*, TeamId, bool battlefield = false);
    void Restore(Player*);
    void Removed(Battleground*, Player*);
    void Reapply(Player*, bool model = true);
    void Resurrect(Player*);
    void Login(Player*);
    void Logout(Player*);
    void Delete(uint64);
    void Update(Player*, uint32);
    void BFEnter(Battlefield*, Player*);
    void BFChoose(Battlefield*, Player*, TeamId&);
    void BFJoined(Battlefield*, Player*, bool);
    void BFLeft(Battlefield*, Player*);
    void BFBeforeEnd(Battlefield*);
    void BFEnded(Battlefield*);
    void BFKill(Battlefield*, Player*, Unit*);
    void Reputation(Player*, uint32&, int32&, bool);
    void TeamChat(Player*, ChatMsg, Language&);
    bool ChooseRace(ChatHandler*, std::string);
    void Debug(ChatHandler*, Player*);
    void BattlefieldList(ChatHandler*, uint32);
    bool Enabled() const { return _enabled; }
private:
    mutable std::recursive_mutex _mutex;
    bool IsWG(Battlefield*) const;
    bool PartyCommitted(Player*, Battleground*);
    void PrepareAV(Battleground*);
    uint8 ChooseVisualRace(Player*, TeamId);
    void MoveQueueGroup(BattlegroundQueue*, uint8, GroupQueueInfo*, uint32);
    bool _enabled = false, _wgEnabled = false, _wgLock = true, _wgPriority = true;
    bool _wgResurrect = true, _levels = true, _items = false, _hunters = false;
    bool _even = false, _random = true, _entry = true, _start = true, _cooldowns = false;
    bool _showPlayerName = false;
    bool _preferencesAvailable = false, _checkedPreferences = false;
    uint32 _groupLimit = 3, _evenThreshold = 0, _hunterMin = 10, _hunterMax = 19, _hunterDiff = 2;
    std::set<uint8> _skipClasses;
    std::map<uint64, CFBGAppearance> _appearances;
    std::map<uint64, uint8> _preferences;
    std::map<uint64, uint32> _updateTimers;
    std::map<uint64, TeamId> _warAssignments;
    std::map<uint64, TeamId> _pendingAssignments;
    std::set<uint32> _crossFactionBGs;
    bool _censusValid = false;
    unsigned _majority = 0, _fairShare = 0, _nativeKept = 0;
};
#endif
