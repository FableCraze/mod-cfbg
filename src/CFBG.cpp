/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#include "CFBG.h"
#include "CFBGBalance.h"
#include "Battlefield.h"
#include "BattlefieldMgr.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "BattlegroundQueue.h"
#include "Chat.h"
#include "Config.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Group.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ReputationMgr.h"
#include "WorldSession.h"
#include "Util.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <random>
#include <fstream>
#include <vector>

namespace
{
uint32 TeamValue(unsigned team) { return team == TEAM_ALLIANCE ? ALLIANCE : HORDE; }
TeamId TeamIndex(uint32 team) { return team == ALLIANCE ? TEAM_ALLIANCE : TEAM_HORDE; }
std::array<std::pair<uint8, char const*>, 15> const VisualRaces = {{
    {1,"human"}, {2,"orc"}, {3,"dwarf"}, {4,"nightelf"}, {5,"undead"},
    {6,"tauren"}, {7,"gnome"}, {8,"troll"}, {9,"goblin"}, {10,"bloodelf"},
    {11,"draenei"}, {22,"worgen"}, {24,"pandaren"}, {25,"pandaren_alliance"}, {26,"pandaren_horde"}
}};
bool ValidRace(Player* p, uint8 race, TeamId team)
{
    ChrRacesEntry const* entry = sChrRacesStore.LookupEntry(race);
    PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race, p->getClass());
    if (!entry || !info || entry->TeamID != uint32(team)) return false;
    uint32 model = p->getGender() == GENDER_FEMALE ? info->displayId_f : info->displayId_m;
    return model && sCreatureDisplayInfoStore.LookupEntry(model);
}
uint32 UnsignedConfig(char const* name, uint32 value)
{
    return uint32(std::max(0, sConfigMgr->GetIntDefault(name, int(value))));
}
}

CFBG& CFBG::Instance() { static CFBG instance; return instance; }

void CFBG::LoadConfig()
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    std::string path = sConfigMgr->GetFilename();
    size_t slash = path.find_last_of("/\\");
    path = slash == std::string::npos ? "" : path.substr(0, slash + 1);
    // The .dist file documents defaults. Only an explicitly installed .conf is loaded.
    if (std::ifstream(path + "CFBG.conf").good()) sConfigMgr->LoadMore((path + "CFBG.conf").c_str());
    _enabled = sConfigMgr->GetBoolDefault("CFBG.Enable", false);
    _wgEnabled = sConfigMgr->GetBoolDefault("CFBG.Battlefield.Enable", true);
    _wgLock = sConfigMgr->GetBoolDefault("CFBG.Battlefield.TeamLock.Enable", true);
    _wgPriority = sConfigMgr->GetBoolDefault("CFBG.Battlefield.NativePriority.Enable", true);
    _wgResurrect = sConfigMgr->GetBoolDefault("CFBG.Battlefield.ReapplyOnResurrect.Enable", true);
    _levels = sConfigMgr->GetBoolDefault("CFBG.BalancedTeams", true);
    _items = sConfigMgr->GetBoolDefault("CFBG.Include.Avg.Ilvl.Enable", false);
    _hunters = sConfigMgr->GetBoolDefault("CFBG.BalancedTeams.Class.LowLevel", false);
    _even = sConfigMgr->GetBoolDefault("CFBG.EvenTeams.Enabled", false);
    _random = sConfigMgr->GetBoolDefault("CFBG.RandomRaceSelection", true);
    _entry = sConfigMgr->GetBoolDefault("CFBG.BalanceTeamsOnEntry.Enabled", true);
    _start = sConfigMgr->GetBoolDefault("CFBG.BalanceTeamsAtStart.Enabled", true);
    _cooldowns = sConfigMgr->GetBoolDefault("CFBG.ResetCooldowns", false);
    _showPlayerName = sConfigMgr->GetBoolDefault("CFBG.Show.PlayerName", false);
    _groupLimit = std::max(1u, UnsignedConfig("CFBG.Players.Count.In.Group", 3));
    _evenThreshold = UnsignedConfig("CFBG.EvenTeams.MaxPlayersThreshold", 0);
    _hunterMin = UnsignedConfig("CFBG.BalancedTeams.Class.MinLevel", 10);
    _hunterMax = UnsignedConfig("CFBG.BalancedTeams.Class.MaxLevel", 19);
    _hunterDiff = UnsignedConfig("CFBG.BalancedTeams.Class.LevelDiff", 2);
    _skipClasses.clear();
    std::string skip = sConfigMgr->GetStringDefault("CFBG.Battlefield.SkipClasses", "");
    std::replace(skip.begin(), skip.end(), ',', ' ');
    std::istringstream input(skip); unsigned c;
    while (input >> c) if (c > 0 && c < MAX_CLASSES) _skipClasses.insert(uint8(c));
    SF_LOG_INFO("module.cfbg", "CFBG %s; existing invitations and war assignments drain normally on disable.", _enabled ? "enabled" : "disabled");
}

void CFBG::MoveQueueGroup(BattlegroundQueue* queue, uint8 bracket, GroupQueueInfo* group, uint32 team)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (group->Team == team) return;
    for (unsigned i = 0; i < BG_QUEUE_GROUP_TYPES_COUNT; ++i)
    {
        auto& list = queue->m_QueuedGroups[bracket][i];
        auto itr = std::find(list.begin(), list.end(), group);
        if (itr == list.end()) continue;
        unsigned target = (i < BG_TEAMS_COUNT ? 0 : BG_TEAMS_COUNT) + unsigned(TeamIndex(team));
        queue->m_QueuedGroups[bracket][target].push_back(group);
        list.erase(itr);
        group->Team = team;
        return;
    }
}

bool CFBG::SelectGroups(BattlegroundQueue* queue, Battleground* bg, uint8 bracket, uint32 minimum, uint32 maximum, bool fill)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (bg->isArena()) return false;
    std::vector<GroupQueueInfo*> groups;
    for (unsigned i = 0; i < BG_QUEUE_GROUP_TYPES_COUNT; ++i)
        for (GroupQueueInfo* g : queue->m_QueuedGroups[bracket][i])
            if (!g->ArenaType && !g->IsRated && !g->IsInvitedToBGInstanceGUID) groups.push_back(g);
    if (!_enabled)
    {
        for (auto* g : groups) MoveQueueGroup(queue, bracket, g, g->NativeTeam);
        return false;
    }
    // Queue timestamps wrap after 49 days; age comparison handles that wrap.
    uint32 now = getMSTime();
    std::stable_sort(groups.begin(), groups.end(), [now](GroupQueueInfo* a, GroupQueueInfo* b)
    { return uint32(now - a->JoinTime) > uint32(now - b->JoinTime); });
    std::vector<CfbgBalance::Group> metrics;
    for (auto* g : groups)
    {
        CfbgBalance::Group m;
        m.size = unsigned(g->Players.size()); m.nativeTeam = TeamIndex(g->NativeTeam);
        for (auto const& member : g->Players)
            if (Player* p = ObjectAccessor::FindPlayer(member.first))
            {
                m.level += p->getLevel(); m.ilvl += p->GetAverageItemLevel();
                if (p->getClass() == CLASS_HUNTER && p->getLevel() + _hunterDiff >= bg->GetMaxLevel()) ++m.hunters;
            }
        if (m.size) { m.level /= m.size; m.ilvl /= m.size; }
        metrics.push_back(m);
    }
    std::array<CfbgBalance::Team, 2> teams{};
    if (fill)
    {
        for (unsigned t = 0; t < 2; ++t) teams[t].count = bg->GetInvitedCount(TeamValue(t));
        // Include reservations not yet inside in the numerical balancing data.
        std::set<uint64> counted;
        for (auto const& member : bg->GetPlayers())
            if (Player* p = ObjectAccessor::FindPlayer(member.first))
            {
                unsigned t = TeamIndex(member.second.Team);
                teams[t].levels += p->getLevel(); teams[t].items += p->GetAverageItemLevel();
                if (p->getClass() == CLASS_HUNTER) ++teams[t].hunters;
                counted.insert(member.first);
            }
        for (unsigned i = 0; i < BG_QUEUE_GROUP_TYPES_COUNT; ++i)
            for (auto* g : queue->m_QueuedGroups[bracket][i])
                if (g->IsInvitedToBGInstanceGUID == bg->GetInstanceID())
                    for (auto const& member : g->Players)
                        if (!counted.count(member.first))
                            if (Player* p = ObjectAccessor::FindPlayer(member.first))
                            {
                                unsigned t = TeamIndex(g->Team);
                                teams[t].levels += p->getLevel(); teams[t].items += p->GetAverageItemLevel();
                                if (p->getClass() == CLASS_HUNTER) ++teams[t].hunters;
                                counted.insert(member.first);
                            }
    }
    CfbgBalance::Options options;
    options.maximum = maximum;
    options.relaxAtCount = _even ? _evenThreshold : 0;
    options.allowedDifference = _even && (!_evenThreshold || std::min(teams[0].count, teams[1].count) < _evenThreshold) ? 0 : 1;
    options.levels = _levels; options.items = _items;
    options.hunters = _hunters && bg->GetMinLevel() >= _hunterMin && bg->GetMaxLevel() <= _hunterMax;
    auto assignment = CfbgBalance::Select(metrics, teams, options);
    std::array<unsigned, 2> selected{};
    for (size_t i = 0; i < groups.size(); ++i) if (assignment[i] >= 0) selected[assignment[i]] += metrics[i].size;
    // A handled but unsuccessful formation must not fall back to native premades.
    if (!fill && (selected[0] < minimum || selected[1] < minimum)) return true;
    for (size_t i = 0; i < groups.size(); ++i)
        if (assignment[i] >= 0)
        {
            unsigned t = unsigned(assignment[i]);
            MoveQueueGroup(queue, bracket, groups[i], TeamValue(t));
            queue->m_SelectionPools[t].AddGroup(groups[i], maximum - teams[t].count);
        }
    return true;
}

bool CFBG::CanJoin(Player* player, Group* group)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!_enabled || !group || (!group->isRaidGroup() && group->GetMembersCount() <= _groupLimit)) return true;
    ChatHandler(player->GetSession()).PSendSysMessage("CFBG groups are limited to %u players; raid groups cannot queue.", _groupLimit);
    return false;
}

bool CFBG::Announce(BattlegroundQueue* queue, Battleground* bg, Player* p, uint8 bracket, uint32 min, uint32 max)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!_enabled || bg->isArena()) return false;
    uint32 count=0;
    for (unsigned i=0;i<BG_QUEUE_GROUP_TYPES_COUNT;++i)
        for (auto* group : queue->m_QueuedGroups[bracket][i])
            if (!group->IsInvitedToBGInstanceGUID) count += uint32(group->Players.size());
    std::ostringstream text;
    text << "[CFBG] " << bg->GetName() << " (" << min << '-' << max << "): " << count << " queued; " << bg->GetMinPlayersPerTeam()*2 << " needed.";
    if (_showPlayerName) text << " Joined: " << p->GetName() << '.';
    ChatHandler handler(p->GetSession());
    if (sWorld->GetBoolConfig(WorldBoolConfigs::CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY)) handler.SendSysMessage(text.str().c_str());
    else handler.SendGlobalSysMessage(text.str().c_str());
    return true;
}

bool CFBG::PartyCommitted(Player* player, Battleground* bg)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    Group* group = player->GetOriginalGroup();
    if (!group) group = player->GetGroup();
    if (!group || group->isBGGroup()) return false;
    auto queueType = BattlegroundMgr::BGQueueTypeId(bg->GetTypeID(), 0);
    auto& queue = sBattlegroundMgr->GetBattlegroundQueue(queueType);
    for (GroupReference* itr = group->GetFirstMember(); itr; itr = itr->next())
    {
        Player* p = itr->GetSource();
        if (!p || p == player) continue;
        if (p->GetBattlegroundId() == bg->GetInstanceID()) return true;
        GroupQueueInfo info;
        if (queue.GetPlayerGroupInfoData(p->GetGUID(), &info) && info.IsInvitedToBGInstanceGUID == bg->GetInstanceID()) return true;
    }
    return false;
}

void CFBG::BeforeAdd(Battleground* bg, Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (bg->isArena()) return;
    if (_enabled || p->GetBGTeam() != p->GetNativeTeam()) _crossFactionBGs.insert(bg->GetInstanceID());
    if (_crossFactionBGs.count(bg->GetInstanceID())) PrepareAV(bg);
    uint32 team = p->GetBGTeam();
    uint32 other = team == ALLIANCE ? HORDE : ALLIANCE;
    if (_enabled && _entry && !PartyCommitted(p, bg) && bg->GetInvitedCount(team) > bg->GetInvitedCount(other) + 1 && bg->GetInvitedCount(other) < bg->GetMaxPlayersPerTeam())
    {
        bg->DecreaseInvitedCount(team); bg->IncreaseInvitedCount(other);
        p->SetBGTeam(other); team = other;
        float x,y,z,o; bg->GetTeamStartLoc(team,x,y,z,o); p->NearTeleportTo(x,y,z,o);
    }
    // Honor already-issued opposite-team invites even after live disable.
    if (_enabled || team != p->GetNativeTeam()) Apply(p, TeamIndex(team));
}
void CFBG::AfterAdd(Battleground* bg, Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (_enabled && !bg->isArena() && _cooldowns) p->RemoveArenaSpellCooldowns(true);
}
void CFBG::BalanceAtStart(Battleground* bg)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (_crossFactionBGs.count(bg->GetInstanceID())) PrepareAV(bg);
    if (!_enabled || !_start || bg->isArena()) return;
    while (true)
    {
        uint32 a = bg->GetPlayersCountByTeam(ALLIANCE), h = bg->GetPlayersCountByTeam(HORDE);
        if (a <= h + 1 && h <= a + 1) break;
        uint32 large = a > h ? ALLIANCE : HORDE, smallerTeam = a > h ? HORDE : ALLIANCE;
        Player* candidate = nullptr;
        for (auto const& member : bg->GetPlayers())
            if (member.second.Team == large)
                if (Player* p = ObjectAccessor::FindPlayer(member.first))
                    if (!PartyCommitted(p, bg)) { candidate = p; break; }
        if (!candidate || !bg->ChangePlayerTeam(candidate, smallerTeam)) break;
        Apply(candidate, TeamIndex(smallerTeam));
        float x,y,z,o; bg->GetTeamStartLoc(smallerTeam,x,y,z,o); candidate->NearTeleportTo(x,y,z,o);
    }
}

void CFBG::PrepareAV(Battleground* bg)
{
    if (bg->GetTypeID(true) != BattlegroundTypeId::BATTLEGROUND_AV) return;
    // Validated against SFDB 26.002. These sixteen commanders use race-specific
    // factions (and neutral difficulty variants). Change instance creatures,
    // never their database templates, so servers without CFBG remain intact.
    for (uint32 i=0; i<bg->BgCreatures.size(); ++i)
        if (bg->BgCreatures[i])
            if (Creature* creature = bg->GetBGCreature(i))
                switch (creature->GetEntry())
                {
                    case 14187: case 14188: case 22528: case 22699:
                    case 31924: case 31963: case 37240: case 37281:
                        creature->setFaction(1216); break;
                    case 14185: case 14186: case 22712: case 22569:
                    case 32049: case 32053: case 37370: case 37374:
                        creature->setFaction(1214); break;
                    default: break;
                }
}

uint8 CFBG::ChooseVisualRace(Player* p, TeamId team)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto preferred = _preferences.find(p->GetGUID());
    if (!_random && preferred != _preferences.end() && preferred->second && ValidRace(p, preferred->second, team)) return preferred->second;
    std::vector<uint8> choices;
    for (auto const& race : VisualRaces) if (ValidRace(p, race.first, team)) choices.push_back(race.first);
    if (choices.empty()) return 0;
    static thread_local std::mt19937 random(std::random_device{}());
    return choices[std::uniform_int_distribution<size_t>(0, choices.size()-1)(random)];
}
void CFBG::Apply(Player* p, TeamId team, bool battlefield)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (team == p->GetNativeTeamId()) { Restore(p); return; }
    auto old = _appearances.find(p->GetGUID());
    if (old != _appearances.end() && old->second.team == team)
    {
        p->SetTemporaryTeam(team);
        Reapply(p);
        return;
    }
    uint32 original = old == _appearances.end() ? p->GetNativeDisplayId() : old->second.originalModel;
    uint8 race = ChooseVisualRace(p, team);
    PlayerInfo const* info = race ? sObjectMgr->GetPlayerInfo(race, p->getClass()) : nullptr;
    uint32 model = info ? (p->getGender() == GENDER_FEMALE ? info->displayId_f : info->displayId_m) : original;
    CFBGAppearance appearance{original, model, race, team, battlefield};
    if (old != _appearances.end())
    {
        appearance.frostwolfReaction=old->second.frostwolfReaction;
        appearance.stormpikeReaction=old->second.stormpikeReaction;
    }
    else
    {
        for (auto entry : {std::pair<uint32,int32*>(1214,&appearance.frostwolfReaction),std::pair<uint32,int32*>(1216,&appearance.stormpikeReaction)})
            if (FactionTemplateEntry const* faction=sFactionTemplateStore.LookupEntry(entry.first))
                if (ReputationRank const* rank=p->GetReputationMgr().GetForcedRankIfAny(faction)) *entry.second=int32(*rank);
    }
    _appearances[p->GetGUID()] = appearance;
    p->SetTemporaryTeam(team);
    Reapply(p);
}
void CFBG::Reapply(Player* p, bool model)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto itr = _appearances.find(p->GetGUID());
    if (itr == _appearances.end()) return;
    auto const& appearance = itr->second;
    // A missing visual candidate must not prevent real team hostility.
    uint8 factionRace = appearance.race ? appearance.race : (appearance.team == TEAM_ALLIANCE ? 1 : 2);
    if (ChrRacesEntry const* race = sChrRacesStore.LookupEntry(factionRace)) p->setFaction(race->FactionID);
    for (Unit* unit : p->m_Controlled) if (unit) unit->setFaction(p->getFaction());
    if (model)
    {
        p->SetNativeDisplayId(appearance.model);
        p->RestoreDisplayId();
    }
    if (p->InBattleground() && p->GetBattleground() && p->GetBattleground()->GetTypeID(true) == BattlegroundTypeId::BATTLEGROUND_AV)
    {
        p->GetReputationMgr().ApplyForceReaction(729, appearance.team == TEAM_HORDE ? REP_FRIENDLY : REP_HOSTILE, true);
        p->GetReputationMgr().ApplyForceReaction(730, appearance.team == TEAM_ALLIANCE ? REP_FRIENDLY : REP_HOSTILE, true);
        if (model) p->GetReputationMgr().SendForceReactions();
    }
}
void CFBG::Restore(Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto itr = _appearances.find(p->GetGUID());
    if (itr == _appearances.end()) return;
    if (p->IsCharmed()) p->RemoveCharmAuras();
    uint32 original = itr->second.originalModel;
    int32 frostwolf=itr->second.frostwolfReaction, stormpike=itr->second.stormpikeReaction;
    _appearances.erase(itr); // Faction-update hook must not reapply during restoration.
    _updateTimers.erase(p->GetGUID());
    p->ClearTemporaryTeam();
    p->SetNativeDisplayId(original); p->RestoreDisplayId();
    for (Unit* unit : p->m_Controlled) if (unit) unit->setFaction(p->getFaction());
    p->GetReputationMgr().ApplyForceReaction(729, frostwolf>=0 ? ReputationRank(frostwolf) : REP_FRIENDLY, frostwolf>=0);
    p->GetReputationMgr().ApplyForceReaction(730, stormpike>=0 ? ReputationRank(stormpike) : REP_FRIENDLY, stormpike>=0);
    p->GetReputationMgr().SendForceReactions();
}

void CFBG::Removed(Battleground* bg, Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    Restore(p);
    if (!bg->GetPlayersSize()) _crossFactionBGs.erase(bg->GetInstanceID());
}
void CFBG::Resurrect(Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto itr = _appearances.find(p->GetGUID());
    if (itr != _appearances.end()) Reapply(p, !itr->second.battlefield || _wgResurrect);
}

void CFBG::Login(Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!_checkedPreferences)
    {
        _preferencesAvailable = bool(CharacterDatabase.Query("SHOW TABLES LIKE 'mod_cfbg_preferences'"));
        _checkedPreferences = true;
        if (!_preferencesAvailable) SF_LOG_WARN("module.cfbg", "Preference table missing; install module character SQL for persistent race choices.");
    }
    if (_preferencesAvailable)
        if (QueryResult result = CharacterDatabase.PQuery("SELECT race FROM mod_cfbg_preferences WHERE guid=%u", p->GetGUIDLow()))
            _preferences[p->GetGUID()] = result->Fetch()[0].GetUInt8();
    if (p->InBattleground() && p->GetBattleground() && !p->GetBattleground()->isArena() && p->GetBGTeam() != p->GetNativeTeam()) Apply(p, TeamIndex(p->GetBGTeam()));
    // A battlefield zone-entry callback can precede OnLogin. Apply the saved
    // preference now if the entry used its provisional random appearance.
    auto appearance = _appearances.find(p->GetGUID());
    if (!_random && appearance != _appearances.end())
    {
        uint8 race = ChooseVisualRace(p, appearance->second.team);
        if (race)
            if (PlayerInfo const* info = sObjectMgr->GetPlayerInfo(race,p->getClass()))
            {
                appearance->second.race = race;
                appearance->second.model = p->getGender()==GENDER_FEMALE ? info->displayId_f : info->displayId_m;
                Reapply(p);
            }
    }
}
void CFBG::Logout(Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    // Battlefield leave executes in RemoveFromWorld after this callback. Keep
    // the assigned team until its team-indexed records have been removed.
    auto appearance = _appearances.find(p->GetGUID());
    if (appearance != _appearances.end() && !appearance->second.battlefield) Restore(p);
    _preferences.erase(p->GetGUID()); _updateTimers.erase(p->GetGUID());
}
void CFBG::Delete(uint64 guid)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    _preferences.erase(guid); _appearances.erase(guid); _warAssignments.erase(guid); _pendingAssignments.erase(guid); _updateTimers.erase(guid);
    if (_preferencesAvailable) CharacterDatabase.PExecute("DELETE FROM mod_cfbg_preferences WHERE guid=%u", GUID_LOPART(guid));
}
void CFBG::Update(Player* p, uint32 diff)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto itr = _appearances.find(p->GetGUID());
    if (itr == _appearances.end()) return;
    uint32& timer = _updateTimers[p->GetGUID()];
    timer += diff; if (timer < 1000) return; timer = 0;
    if (!itr->second.battlefield)
    {
        if (!p->InBattleground()) Restore(p);
        else Reapply(p, false);
    }
}

bool CFBG::IsWG(Battlefield* bf) const {
    std::lock_guard<std::recursive_mutex> guard(_mutex); return bf && bf->GetTypeId() == BATTLEFIELD_WG; }
void CFBG::BFEnter(Battlefield* bf, Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf) || !bf->IsWarTime() || p->InBattleground()) return;
    auto lock = _warAssignments.find(p->GetGUID());
    if (lock != _warAssignments.end() && (_wgLock || !_enabled) && bf->TransferPlayerTeam(p, lock->second))
        Apply(p, lock->second, true);
    else if (_enabled && _wgEnabled && !_skipClasses.count(p->getClass()) && !bf->HasWarVacancy(p->GetNativeTeamId(), p->GetGUID()))
    {
        TeamId opposite = p->GetNativeTeamId() == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;
        if (bf->HasWarVacancy(opposite, p->GetGUID()) && bf->TransferPlayerTeam(p, opposite))
            Apply(p, opposite, true);
    }
}
void CFBG::BFChoose(Battlefield* bf, Player* p, TeamId& team)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf) || p->InBattleground()) return;
    auto lock = _warAssignments.find(p->GetGUID());
    if (lock != _warAssignments.end() && (_wgLock || !_enabled)) { team = lock->second; return; }
    if (!_enabled || !_wgEnabled || _skipClasses.count(p->getClass())) return;
    if (!_censusValid)
    {
        std::set<uint64> census;
        for (unsigned t = 0; t < 2; ++t)
        {
            census.insert(bf->GetPlayersInWar(TeamId(t)).begin(), bf->GetPlayersInWar(TeamId(t)).end());
            for (auto const& invite : bf->GetInvitedPlayers(TeamId(t))) census.insert(invite.first);
        }
        std::array<unsigned,2> natives{};
        for (uint64 guid : census) if (Player* member = ObjectAccessor::FindPlayer(guid)) ++natives[member->GetNativeTeamId()];
        _majority = natives[0] >= natives[1] ? 0 : 1;
        _fairShare = (natives[0] + natives[1] + 1) / 2; _nativeKept = 0; _censusValid = true;
    }
    std::array<unsigned,2> live = {{unsigned(bf->GetPlayersInWar(TEAM_ALLIANCE).size()), unsigned(bf->GetPlayersInWar(TEAM_HORDE).size())}};
    team = TeamId(CfbgBalance::WintergraspTeam(p->GetNativeTeamId(), _majority, _fairShare, _nativeKept, live, _wgPriority));
    if (!bf->HasWarVacancy(team, p->GetGUID()))
    {
        TeamId other = team == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;
        team = bf->HasWarVacancy(other, p->GetGUID()) ? other : p->GetTeamId();
    }
    _pendingAssignments[p->GetGUID()] = team;
}
void CFBG::BFJoined(Battlefield* bf, Player* p, bool success)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf)) return;
    auto pending = _pendingAssignments.find(p->GetGUID());
    if (pending != _pendingAssignments.end())
    {
        if (success)
        {
            if (!_warAssignments.count(p->GetGUID()) && p->GetNativeTeamId() == _majority && pending->second == _majority) ++_nativeKept;
            _warAssignments[p->GetGUID()] = pending->second;
        }
        _pendingAssignments.erase(pending);
    }
    if (success && (_enabled && _wgEnabled || _warAssignments.count(p->GetGUID()))) Apply(p, p->GetTeamId(), true);
    if (success && _appearances.count(p->GetGUID())) _warAssignments[p->GetGUID()] = p->GetTeamId();
    if (!success)
    {
        bf->TransferPlayerTeam(p, p->GetNativeTeamId());
        Restore(p);
    }
}
void CFBG::BFLeft(Battlefield* bf, Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf)) return;
    _pendingAssignments.erase(p->GetGUID());
    auto itr = _appearances.find(p->GetGUID());
    if (itr != _appearances.end() && itr->second.battlefield)
    {
        bf->TransferPlayerTeam(p, p->GetNativeTeamId());
        Restore(p);
    }
}
void CFBG::BFBeforeEnd(Battlefield* bf)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf)) return;
    // Skyfire grants honor/achievements itself. Only complete the native
    // version of the victory quest for winners; never recast reward spells.
    for (uint64 guid : bf->GetPlayersInWar(bf->GetDefenderTeam()))
        if (Player* p = ObjectAccessor::FindPlayer(guid))
            if (_warAssignments.count(guid)) p->AreaExploredOrEventHappens(p->GetNativeTeamId() == TEAM_ALLIANCE ? 13181 : 13183);
}
void CFBG::BFEnded(Battlefield* bf)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf)) return;
    std::set<uint64> affected;
    for (auto const& entry : _appearances) if (entry.second.battlefield) affected.insert(entry.first);
    for (uint64 guid : affected)
        if (Player* p = ObjectAccessor::FindPlayer(guid))
        {
            bf->TransferPlayerTeam(p, p->GetNativeTeamId()); Restore(p);
        }
    _warAssignments.clear(); _pendingAssignments.clear(); _censusValid = false;
}
void CFBG::BFKill(Battlefield* bf, Player* killer, Unit* victim)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!IsWG(bf) || !killer || !victim || killer == victim || victim->GetTypeId() != TypeID::TYPEID_PLAYER) return;
    if (!_enabled && !_warAssignments.count(killer->GetGUID())) return;
    killer->KilledMonsterCredit(killer->GetNativeTeamId() == TEAM_ALLIANCE ? 31086 : 39019);
}
void CFBG::Reputation(Player* p, uint32& faction, int32&, bool)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (!_appearances.count(p->GetGUID()) || !p->InBattleground()) return;
    if (faction == 729 && p->GetNativeTeamId() == TEAM_ALLIANCE) faction = 730;
    else if (faction == 730 && p->GetNativeTeamId() == TEAM_HORDE) faction = 729;
}
void CFBG::TeamChat(Player* p, ChatMsg type, Language& lang)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    if (lang == Language::LANG_ADDON) return;
    Battleground* bg = p->GetBattleground();
    Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(p->GetZoneId());
    bool bgActive = bg && !bg->isArena() && (_enabled || _crossFactionBGs.count(bg->GetInstanceID()));
    bool bfActive = IsWG(bf) && bf->GetPlayersInWar(p->GetTeamId()).count(p->GetGUID()) && (_enabled && _wgEnabled || _warAssignments.count(p->GetGUID()));
    if ((bgActive || bfActive) && (type == ChatMsg::CHAT_MSG_PARTY || type == ChatMsg::CHAT_MSG_RAID || type == ChatMsg::CHAT_MSG_RAID_WARNING || type == ChatMsg::CHAT_MSG_INSTANCE || type == ChatMsg::CHAT_MSG_INSTANCE_LEADER)) lang = Language::LANG_UNIVERSAL;
}
bool CFBG::ChooseRace(ChatHandler* handler, std::string input)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    Player* p = handler->GetSession()->GetPlayer();
    if (_random) { handler->SendSysMessage("Race selection is disabled by CFBG.RandomRaceSelection."); return true; }
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    uint8 race = 0; bool found = input == "random";
    TeamId opposite = p->GetNativeTeamId() == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;
    for (auto const& candidate : VisualRaces)
        if (input == candidate.second) { race = candidate.first; found = ValidRace(p, race, opposite); break; }
    if (!found) { handler->SendSysMessage("Invalid opposite-faction race/class/model. Use random to reset."); return false; }
    _preferences[p->GetGUID()] = race;
    if (_preferencesAvailable) CharacterDatabase.PExecute("REPLACE INTO mod_cfbg_preferences (guid,race) VALUES (%u,%u)", p->GetGUIDLow(), uint32(race));
    handler->SendSysMessage(_preferencesAvailable ? "CFBG race preference saved; applies on your next PvP entry." : "CFBG preference set for this session; character SQL is not installed.");
    return true;
}
void CFBG::Debug(ChatHandler* handler, Player* p)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    auto state = _appearances.find(p->GetGUID());
    auto pref = _preferences.find(p->GetGUID());
    handler->PSendSysMessage("CFBG %s: enabled=%u native=%u current=%u BG=%u realRace=%u preference=%u temporary=%u model=%u nativeModel=%u", p->GetName().c_str(), _enabled, uint32(p->GetNativeTeamId()), uint32(p->GetTeamId()), p->GetBGTeam(), uint32(p->getRace()), pref == _preferences.end() ? 0u : uint32(pref->second), p->HasTemporaryTeam(), p->GetDisplayId(), p->GetNativeDisplayId());
    if (state != _appearances.end()) handler->PSendSysMessage("Visual race=%u model=%u originalModel=%u battlefield=%u", uint32(state->second.race), state->second.model, state->second.originalModel, state->second.battlefield);
    auto lock = _warAssignments.find(p->GetGUID());
    if (lock != _warAssignments.end()) handler->PSendSysMessage("Wintergrasp war assignment=%u", uint32(lock->second));
}
void CFBG::BattlefieldList(ChatHandler* handler, uint32 id)
{
    std::lock_guard<std::recursive_mutex> guard(_mutex);
    Battlefield* bf = sBattlefieldMgr->GetBattlefieldByBattleId(id);
    if (!bf) { handler->SendSysMessage("Battlefield not found."); return; }
    handler->PSendSysMessage("Battlefield %u: %s, max=%u per team", id, bf->IsWarTime() ? "WAR" : "PEACE", bf->GetMaxPlayersPerTeam());
    for (unsigned t=0;t<2;++t) handler->PSendSysMessage("%s: zone=%u queue=%u invited=%u war=%u", t ? "Horde" : "Alliance", uint32(bf->GetPlayersInZone(TeamId(t)).size()), uint32(bf->GetPlayersInQueue(TeamId(t)).size()), uint32(bf->GetInvitedPlayers(TeamId(t)).size()), uint32(bf->GetPlayersInWar(TeamId(t)).size()));
}
