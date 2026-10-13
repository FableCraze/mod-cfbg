#include "CFBGBalance.h"
#include <cstdlib>
#include <iostream>
#ifdef CFBG_INTEGRATION_TESTS
#include "CFBGAppearance.h"
#include "Chat.h"
#include "DatabaseEnv.h"
#include "ScriptMgr.h"

uint32 realmID = 0;
DatabaseWorkerPool<WorldDatabaseConnection> WorldDatabase;
DatabaseWorkerPool<CharacterDatabaseConnection> CharacterDatabase;
DatabaseWorkerPool<LoginDatabaseConnection> LoginDatabase;
std::map<uint32, std::string> realmNameStore;
void AddSC_CFBG();
void AddSC_cfbg_commandscript();
void AddSC_cfbg_bf_commandscript();
#endif

void Check(bool ok, char const* message)
{
    if (!ok) { std::cerr << message << '\n'; std::exit(1); }
}
#ifdef CFBG_INTEGRATION_TESTS
class BattlefieldCommands : public CommandScript
{
public:
    BattlefieldCommands() : CommandScript("cfbg_test_bf_commands") { }
    std::vector<ChatCommand> GetCommands() const override
    {
        return {{"bf", 0, true, nullptr, "", {{"existing", 0, true, nullptr, ""}}}};
    }
};
void RegisterModuleScripts()
{
    AddSC_CFBG();
    AddSC_cfbg_commandscript();
    AddSC_cfbg_bf_commandscript();
}
void CheckModuleCommands()
{
    auto commands = sScriptMgr->GetChatCommands();
    Check(commands.size() == 2, "Unexpected command roots");
    bool cfbg = false, bf = false;
    for (auto const& command : commands)
    {
        if (std::string(command.Name) == "cfbg")
        {
            cfbg = true;
            Check(command.ChildCommands.size() == 2, "CFBG subcommands changed");
            auto const& race = command.ChildCommands[0];
            auto const& debug = command.ChildCommands[1];
            Check(std::string(race.Name) == "race" && race.Handler &&
                race.Permission == rbac::RBAC_PERM_COMMAND_COMMANDS && !race.AllowConsole &&
                race.Help == ".cfbg race <race|random>", "Race command contract changed");
            Check(std::string(debug.Name) == "debug" && debug.Handler &&
                debug.Permission == rbac::RBAC_PERM_COMMAND_DEBUG_BG && debug.AllowConsole &&
                debug.Help == ".cfbg debug [online player]", "Debug command contract changed");
        }
        if (std::string(command.Name) == "bf")
        {
            bf = true;
            Check(command.ChildCommands.size() == 2, "BF commands duplicated or lost");
            Check(std::string(command.ChildCommands[0].Name) == "existing", "Existing BF command changed");
            auto const& list = command.ChildCommands[1];
            Check(std::string(list.Name) == "list" && list.Handler &&
                list.Permission == rbac::RBAC_PERM_COMMAND_BF && !list.AllowConsole &&
                list.Help == ".bf list <battleId>", "BF list command contract changed");
        }
    }
    Check(cfbg && bf, "Missing command roots");
}
void CheckModuleScriptLifetime()
{
    for (int cycle = 0; cycle < 3; ++cycle)
    {
        new BattlefieldCommands(); // Owned and deleted by ScriptMgr::Unload.
        auto count = sScriptMgr->GetScriptCount();
        RegisterModuleScripts();
        Check(sScriptMgr->GetScriptCount() == count + 6, "Missing module scripts");
        CheckModuleCommands();
        RegisterModuleScripts();
        Check(sScriptMgr->GetScriptCount() == count + 6, "Duplicate registration");
        CheckModuleCommands();
        sScriptMgr->Unload();
        Check(sScriptMgr->GetChatCommands().empty(), "Unload retained command scripts");
        Check(!sScriptMgr->SelectBGGroups(nullptr, nullptr, 0, 1, 10, false), "Unload retained PvP scripts");
    }
    RegisterModuleScripts(); // Also exercise process-exit cleanup without Unload.
}
void CheckVisualRaceSelection()
{
    Check(ChooseCFBGVisualIndex(0) == 0, "Empty visual selection must be safe");
    for (std::size_t count = 1; count <= 15; ++count)
        for (int draw = 0; draw < 128; ++draw)
            Check(ChooseCFBGVisualIndex(count) < count, "Visual selection exceeded valid races");
}
#endif
std::array<unsigned,2> Counts(std::vector<CfbgBalance::Group> const& groups, std::vector<int> const& result)
{
    std::array<unsigned,2> counts{};
    for (size_t i=0;i<groups.size();++i) if(result[i]>=0) counts[result[i]] += groups[i].size;
    return counts;
}
void CheckPremadePopulations(unsigned maximum, unsigned size)
{
    CfbgBalance::Options options;
    options.maximum = maximum;
    for (unsigned population = 0; population <= 15; ++population)
    {
        std::vector<CfbgBalance::Group> groups(population);
        for (auto& group : groups) { group.size = size; group.nativeTeam = 0; }
        auto counts = Counts(groups, CfbgBalance::Select(groups, {}, options));
        Check(counts[0] <= maximum && counts[1] <= maximum, "Selection capacity exceeded");
        Check(counts[0] % size == 0 && counts[1] % size == 0, "Premade was split");
    }
}
void CheckMetricPriority()
{
    using namespace CfbgBalance;
    Options options;
    options.maximum = 5;
    CfbgBalance::Group group;
    group.size = 1;
    group.nativeTeam = 1;
    std::vector<CfbgBalance::Group> groups = {group};
    std::array<CfbgBalance::Team, 2> teams = {{{1, 10, 100, 0}, {1, 20, 50, 1}}};
    Check(Select(groups, teams, options)[0] == 0, "Level balance must precede item balance");
    options.levels = false;
    options.items = true;
    Check(Select(groups, teams, options)[0] == 1, "Item balance must select the lower item total");
    options.items = false;
    options.hunters = true;
    Check(Select(groups, teams, options)[0] == 0, "Hunter balance must select the lower hunter count");
    options.hunters = false;
    Check(Select(groups, teams, options)[0] == 1, "Equal headcounts without metrics must prefer the native team");
    teams[0].count = 2;
    options.items = true;
    options.hunters = true;
    Check(Select(groups, teams, options)[0] == 1, "Headcount must precede all metric preferences");
}
void CheckPackingPriority()
{
    using namespace CfbgBalance;
    Options options;
    options.maximum = 1;
    options.allowedDifference = 0;
    std::vector<CfbgBalance::Group> groups(3);
    for (auto& group : groups) group.size = 1;
    auto result = Pack(groups, {}, options);
    Check(result[0] >= 0 && result[1] >= 0 && result[0] != result[1] && result[2] == -1,
        "Packing must keep earlier candidates when scores tie");
    groups[0].nativeTeam = 2;
    result = Select(groups, {}, options);
    Check(result == std::vector<int>({-1, 0, 1}), "Invalid native teams must remain waiting");
}
int main()
{
#ifdef CFBG_INTEGRATION_TESTS
    CheckVisualRaceSelection();
    CheckModuleScriptLifetime();
#endif
    CheckMetricPriority();
    CheckPackingPriority();
    using namespace CfbgBalance;
    Options o; o.maximum=10;
    std::vector<CfbgBalance::Group> solos(5); for(auto& g:solos) {g.size=1;g.nativeTeam=0;g.level=90;g.ilvl=450;}
    auto result=Select(solos,{},o); auto counts=Counts(solos,result);
    Check(counts[0]+counts[1]==5 && counts[0] && counts[1],"Single-faction players must form mixed teams");
    Check(counts[0]<=counts[1]+1 && counts[1]<=counts[0]+1,"Odd population must differ by at most one");
    o.allowedDifference=0;
    counts=Counts(solos,Select(solos,{},o));
    Check(counts[0]==counts[1] && counts[0]+counts[1]==4,"Strict even mode leaves one solo waiting");
    o.relaxAtCount=2;
    counts=Counts(solos,Select(solos,{},o));
    Check(counts[0]+counts[1]==5,"Even threshold must relax during formation, not only reinforcement");
    o.relaxAtCount=0;
    CfbgBalance::Group party; party.size=3; party.nativeTeam=0; party.level=90;
    std::vector<CfbgBalance::Group> premades={party,party};
    counts=Counts(premades,Select(premades,{},o));
    Check(counts[0]==3 && counts[1]==3,"Premades must remain atomic across mixed teams");
    o.maximum=2;
    result=Select(premades,{},o);
    Check(result[0]==-1 && result[1]==-1,"Oversized premades must wait");
    o.maximum=5;
    std::vector<CfbgBalance::Group> fragmented(4);
    for(size_t i=0;i<4;++i){fragmented[i].size=i%2?3:2;fragmented[i].nativeTeam=0;}
    counts=Counts(fragmented,Select(fragmented,{},o));
    Check(counts[0]==5 && counts[1]==5,"Queue must find 2+3 versus 2+3 instead of fragmenting reservations");
    fragmented.resize(2);
    counts=Counts(fragmented,Select(fragmented,{},o));
    Check(counts[0]==0 && counts[1]==0,"Strict even mode must not split or invite an unequal 2-player/3-player pair");
    o.maximum=5; o.allowedDifference=1;
    std::array<CfbgBalance::Team,2> base{}; base[0].count=5;base[1].count=3;
    counts=Counts(solos,Select(solos,base,o));
    Check(counts[0]==0 && counts[1]<=2,"Pending invitations count against capacity");
    Check(WintergraspTeam(1,0,3,3,{{4,1}},true)==1,"Native minority must stay native");
    Check(WintergraspTeam(0,0,3,2,{{3,0}},true)==0,"Majority keeps its native fair share");
    Check(WintergraspTeam(0,0,3,3,{{3,1}},true)==1,"Majority surplus must join smaller side");
    Check(WintergraspTeam(0,0,3,3,{{1,3}},true)==0,"Flip must not worsen live balance");
    // Explore capacities, population and premade sizes; check no duplicate or
    // partial allocation and no team exceeding its reservation budget.
    for(unsigned max=1;max<=12;++max)
        for(unsigned size=1;size<=5;++size)
            CheckPremadePopulations(max, size);
    std::cout << "CFBG balance scenarios passed\n";
}
