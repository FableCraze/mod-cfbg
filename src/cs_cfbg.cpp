/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#include "CFBG.h"
#include "Chat.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "RBAC.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include <sstream>
#include <memory>
class cfbg_commandscript : public CommandScript
{
public:
    cfbg_commandscript() : CommandScript("cfbg_commandscript") { }
    std::vector<ChatCommand> GetCommands() const override
    {
        return {{"cfbg", rbac::RBAC_PERM_COMMAND_COMMANDS, true, nullptr, "", {
            {"race", rbac::RBAC_PERM_COMMAND_COMMANDS, false, Race, ".cfbg race <race|random>"},
            {"debug", rbac::RBAC_PERM_COMMAND_DEBUG_BG, true, Debug, ".cfbg debug [online player]"}
        }}};
    }
    static bool Race(ChatHandler* h, char const* args)
    { return CFBG::Instance().ChooseRace(h, args ? args : ""); }
    static bool Debug(ChatHandler* h, char const* args)
    {
        Player* p = args && *args ? ObjectAccessor::FindPlayerByName(args) : h->getSelectedPlayer();
        if (!p && h->GetSession() && (!args || !*args)) p = h->GetSession()->GetPlayer();
        if (!p) { h->SendSysMessage("Online player not found."); return false; }
        CFBG::Instance().Debug(h, p); return true;
    }
};
class cfbg_bf_commandscript : public CommandScript
{
public:
    cfbg_bf_commandscript() : CommandScript("cfbg_bf_commandscript") { }
    std::vector<ChatCommand> GetCommands() const override
    {
        return {};
    }
    void ModifyCommands(std::vector<ChatCommand>& commands) const override
    {
        for (auto& command : commands)
            if (std::string(command.Name) == "bf")
            {
                command.ChildCommands.emplace_back("list", rbac::RBAC_PERM_COMMAND_BF, false, List, ".bf list <battleId>");
                return;
            }
    }
    static bool List(ChatHandler* h, char const* args)
    {
        std::istringstream input(args ? args : ""); uint32 id; std::string extra;
        if (!(input >> id) || (input >> extra)) return false;
        CFBG::Instance().BattlefieldList(h, id); return true;
    }
};
// Script constructors register themselves; ScriptMgr owns and deletes them.
void AddSC_cfbg_commandscript() { std::make_unique<cfbg_commandscript>().release(); }
void AddSC_cfbg_bf_commandscript() { std::make_unique<cfbg_bf_commandscript>().release(); }
