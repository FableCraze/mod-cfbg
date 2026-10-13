/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#ifndef CFBG_H
#define CFBG_H
#include "SharedDefines.h"
#include <array>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
class Player;
class Group;
class Battleground;
class BattlegroundQueue;
class Battlefield;
class ChatHandler;
class Unit;
class WorldSocket;
class WorldPacket;
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
    bool mirrorImageOwned = false;
    std::vector<uint8> mirrorData;
    uint64 corpse = 0;
    uint32 corpseSkin = 0;
    uint32 corpseFacialHair = 0;
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
    void MirrorImageData(WorldSocket*, WorldPacket&);
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
    bool Enabled() const { return _bgConfig.enabled; }
private:
    // Player faction changes synchronously re-enter Reapply through PvPScript.
    mutable std::recursive_mutex _mutex;
    bool IsWG(Battlefield*) const;
    bool PartyCommitted(Player*, Battleground*);
    Player* BalanceCandidate(Battleground*, uint32);
    void PrepareAV(Battleground*);
    uint8 ChooseVisualRace(Player*, TeamId);
    void RefreshAppearance(Player*, CFBGAppearance&) const;
    void SendAppearance(Player*, CFBGAppearance&) const;
    void RefreshCorpseAppearance(Player const*, CFBGAppearance&) const;
    void MoveQueueGroup(BattlegroundQueue*, uint8, GroupQueueInfo*, uint32);
    struct BattlegroundConfig
    {
        bool enabled = false;
        bool levels = true;
        bool items = false;
        bool hunters = false;
        bool even = false;
        bool random = true;
        bool entry = true;
        bool start = true;
        bool cooldowns = false;
        bool showPlayerName = false;
        uint32 groupLimit = 3;
        uint32 evenThreshold = 0;
        uint32 hunterMin = 10;
        uint32 hunterMax = 19;
        uint32 hunterDiff = 2;
    } _bgConfig;
    struct BattlefieldConfig
    {
        bool enabled = false;
        bool lock = true;
        bool priority = true;
        bool resurrect = true;
        std::set<uint8> skipClasses;
    } _bfConfig;
    bool _preferencesAvailable = false;
    bool _checkedPreferences = false;
    std::map<uint64, CFBGAppearance> _appearances;
    std::map<uint64, uint8> _preferences;
    std::map<uint64, uint32> _updateTimers;
    std::map<uint64, TeamId> _warAssignments;
    std::map<uint64, TeamId> _pendingAssignments;
    std::set<uint32> _crossFactionBGs;
    bool _censusValid = false;
    unsigned _majority = 0;
    unsigned _fairShare = 0;
    unsigned _nativeKept = 0;
};
#endif
