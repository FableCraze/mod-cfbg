/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#include "CFBG.h"
#include "CFBGScriptLifetime.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"

class CFBGPvP : public PvPScript
{
public:
    CFBGPvP() : PvPScript("CFBGPvP") { }
    CFBGPvP(CFBGPvP const&) = delete;
    CFBGPvP& operator=(CFBGPvP const&) = delete;
    CFBGPvP(CFBGPvP&&) = delete;
    CFBGPvP& operator=(CFBGPvP&&) = delete;
    ~CFBGPvP() override { CFBGScriptLifetime<CFBGPvP>::Destroyed(this); }
    bool SelectBGGroups(BattlegroundQueue* q, Battleground* bg, uint8 b, uint32 min, uint32 max, bool fill) override
    { return CFBG::Instance().SelectGroups(q, bg, b, min, max, fill); }
    bool CanJoinBG(Player* p, Group* g) override { return CFBG::Instance().CanJoin(p, g); }
    bool HandleBGQueueAnnounce(BattlegroundQueue* q, Battleground* bg, Player* p, uint8 b, uint32 min, uint32 max) override
    { return CFBG::Instance().Announce(q,bg,p,b,min,max); }
    void BeforeBGAddPlayer(Battleground* bg, Player* p) override { CFBG::Instance().BeforeAdd(bg, p); }
    void AfterBGAddPlayer(Battleground* bg, Player* p) override { CFBG::Instance().AfterAdd(bg, p); }
    void AfterBGRemovePlayer(Battleground* bg, Player* p) override { CFBG::Instance().Removed(bg,p); }
    void BeforeBGStart(Battleground* bg) override { CFBG::Instance().BalanceAtStart(bg); }
    void OnFactionUpdate(Player* p) override { CFBG::Instance().Reapply(p, false); }
    void OnResurrect(Player* p) override { CFBG::Instance().Resurrect(p); }
    void BeforeTeamChat(Player* p, ChatMsg t, Language& l) override { CFBG::Instance().TeamChat(p, t, l); }
    void BeforeReputation(Player* p, uint32& f, int32& a, bool incremental) override
    { CFBG::Instance().Reputation(p, f, a, incremental); }
    void BeforeBFEnter(Battlefield* bf, Player* p) override { CFBG::Instance().BFEnter(bf, p); }
    void BeforeBFJoin(Battlefield* bf, Player* p, TeamId& t) override { CFBG::Instance().BFChoose(bf, p, t); }
    void AfterBFJoin(Battlefield* bf, Player* p, bool ok) override { CFBG::Instance().BFJoined(bf, p, ok); }
    void AfterBFLeave(Battlefield* bf, Player* p) override { CFBG::Instance().BFLeft(bf, p); }
    void BeforeBFEnd(Battlefield* bf) override { CFBG::Instance().BFBeforeEnd(bf); }
    void AfterBFEnd(Battlefield* bf) override { CFBG::Instance().BFEnded(bf); }
    void OnBFKill(Battlefield* bf, Player* p, Unit* v) override { CFBG::Instance().BFKill(bf, p, v); }
};
class CFBGNetwork : public ServerScript
{
public:
    CFBGNetwork() : ServerScript("CFBGNetwork") { }
    CFBGNetwork(CFBGNetwork const&) = delete;
    CFBGNetwork& operator=(CFBGNetwork const&) = delete;
    CFBGNetwork(CFBGNetwork&&) = delete;
    CFBGNetwork& operator=(CFBGNetwork&&) = delete;
    ~CFBGNetwork() override { CFBGScriptLifetime<CFBGNetwork>::Destroyed(this); }
    void OnPacketReceive(WorldSocket* socket, WorldPacket& packet) override
    {
        if (packet.GetOpcode() == CMSG_GET_MIRROR_IMAGE_DATA)
            CFBG::Instance().MirrorImageData(socket, packet);
    }
};
class CFBGPlayers : public PlayerScript
{
public:
    CFBGPlayers() : PlayerScript("CFBGPlayers") { }
    CFBGPlayers(CFBGPlayers const&) = delete;
    CFBGPlayers& operator=(CFBGPlayers const&) = delete;
    CFBGPlayers(CFBGPlayers&&) = delete;
    CFBGPlayers& operator=(CFBGPlayers&&) = delete;
    ~CFBGPlayers() override { CFBGScriptLifetime<CFBGPlayers>::Destroyed(this); }
    void OnLogin(Player* p, bool) override { CFBG::Instance().Login(p); }
    void OnLogout(Player* p) override { CFBG::Instance().Logout(p); }
    void OnDelete(uint64 guid) override { CFBG::Instance().Delete(guid); }
    void OnUpdate(Player* p, uint32 diff) override { CFBG::Instance().Update(p, diff); }
};
class CFBGWorld : public WorldScript
{
public:
    CFBGWorld() : WorldScript("CFBGWorld") { }
    CFBGWorld(CFBGWorld const&) = delete;
    CFBGWorld& operator=(CFBGWorld const&) = delete;
    CFBGWorld(CFBGWorld&&) = delete;
    CFBGWorld& operator=(CFBGWorld&&) = delete;
    ~CFBGWorld() override { CFBGScriptLifetime<CFBGWorld>::Destroyed(this); }
    void OnConfigLoad(bool) override { CFBG::Instance().LoadConfig(); }
};
void AddSC_CFBG()
{
    CFBGScriptLifetime<CFBGPvP>::Create();
    CFBGScriptLifetime<CFBGNetwork>::Create();
    CFBGScriptLifetime<CFBGPlayers>::Create();
    CFBGScriptLifetime<CFBGWorld>::Create();
}
