/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#ifndef CFBG_BALANCE_H
#define CFBG_BALANCE_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace CfbgBalance
{
struct Group
{
    unsigned size = 0;
    unsigned nativeTeam = 0;
    double level = 0;
    double ilvl = 0;
    unsigned hunters = 0;
};
struct Team
{
    unsigned count = 0;
    double levels = 0;
    double items = 0;
    unsigned hunters = 0;
};
struct Options
{
    unsigned maximum = 0;
    unsigned allowedDifference = 1;
    unsigned relaxAtCount = 0;
    bool levels = true;
    bool items = false;
    bool hunters = false;
};
namespace Detail
{
inline unsigned Difference(unsigned a, unsigned b) { return a > b ? a - b : b - a; }
inline unsigned AllowedDifference(unsigned a, unsigned b, Options options)
{
    return options.relaxAtCount && std::min(a, b) >= options.relaxAtCount ? 1 : options.allowedDifference;
}
struct Score
{
    bool acceptable = false;
    unsigned total = 0;
    unsigned difference = ~0u;
};
inline Score MakeScore(unsigned a, unsigned h, unsigned total, Options options)
{
    unsigned difference = Difference(a, h);
    return {difference <= AllowedDifference(a, h, options), total, difference};
}
inline bool Better(Score candidate, Score current)
{
    if (candidate.acceptable != current.acceptable) return candidate.acceptable;
    if (candidate.acceptable)
        return candidate.total > current.total ||
            (candidate.total == current.total && candidate.difference < current.difference);
    return candidate.difference < current.difference ||
        (candidate.difference == current.difference && candidate.total > current.total);
}
struct Node
{
    int parent;
    size_t group;
    int team;
};
class PackingStates
{
public:
    PackingStates(std::array<Team, 2> base, Options options)
        : _base(base),
          _capA(base[0].count >= options.maximum ? 0 : options.maximum - base[0].count),
          _capH(base[1].count >= options.maximum ? 0 : options.maximum - base[1].count),
          _stride(_capH + 1), _states((_capA + 1) * _stride, -1)
    {
        _nodes.emplace_back(-1, 0, -1);
        _states[0] = 0;
    }
    void Add(Group const& group, size_t index)
    {
        auto next = _states;
        for (unsigned a = 0; a <= _capA; ++a)
            for (unsigned h = 0; h <= _capH; ++h)
                AddAt(group, index, a, h, next);
        _states.swap(next);
    }
    bool Full() const { return _states[_capA * _stride + _capH] >= 0; }
    std::vector<int> Result(size_t groupCount, Options options) const
    {
        Score bestScore;
        int best = 0;
        for (unsigned a = 0; a <= _capA; ++a)
            for (unsigned h = 0; h <= _capH; ++h)
            {
                int state = _states[a * _stride + h];
                if (state < 0) continue;
                auto score = MakeScore(_base[0].count + a, _base[1].count + h, a + h, options);
                if (Better(score, bestScore))
                {
                    bestScore = score;
                    best = state;
                }
            }
        std::vector result(groupCount, -1);
        for (int node = best; node > 0; node = _nodes[node].parent)
            result[_nodes[node].group] = _nodes[node].team;
        return result;
    }
private:
    void AddAt(Group const& group, size_t index, unsigned a, unsigned h, std::vector<int>& next)
    {
        int parent = _states[a * _stride + h];
        if (parent < 0) return;
        unsigned preferred = group.nativeTeam;
        if (_base[0].count + a != _base[1].count + h)
            preferred = _base[0].count + a < _base[1].count + h ? 0 : 1;
        for (unsigned pass = 0; pass < 2; ++pass)
        {
            unsigned team = pass ? 1 - preferred : preferred;
            unsigned aa = a + (team == 0 ? group.size : 0);
            unsigned hh = h + (team == 1 ? group.size : 0);
            if (aa > _capA || hh > _capH || next[aa * _stride + hh] >= 0) continue;
            next[aa * _stride + hh] = int(_nodes.size());
            _nodes.emplace_back(parent, index, int(team));
        }
    }
    std::array<Team, 2> _base;
    unsigned _capA;
    unsigned _capH;
    unsigned _stride;
    std::vector<Node> _nodes;
    std::vector<int> _states;
};
inline void Apply(std::array<Team, 2>& teams, Group const& group, unsigned team, int sign)
{
    if (sign > 0) teams[team].count += group.size;
    else teams[team].count -= group.size;
    teams[team].levels += sign * group.level * group.size;
    teams[team].items += sign * group.ilvl * group.size;
    if (sign > 0) teams[team].hunters += group.hunters;
    else teams[team].hunters -= group.hunters;
}
inline unsigned PreferredTeam(Group const& group, std::array<Team, 2> const& teams, Options options)
{
    if (teams[0].count != teams[1].count) return teams[0].count < teams[1].count ? 0 : 1;
    if (options.levels && teams[0].levels != teams[1].levels) return teams[0].levels < teams[1].levels ? 0 : 1;
    if ((options.items || options.levels) && teams[0].items != teams[1].items) return teams[0].items < teams[1].items ? 0 : 1;
    if (options.hunters && teams[0].hunters != teams[1].hunters) return teams[0].hunters < teams[1].hunters ? 0 : 1;
    return group.nativeTeam;
}
inline bool RepairOne(std::vector<Group> const& groups, std::vector<int>& result,
    std::array<Team, 2>& teams, unsigned large, Options options)
{
    unsigned small = 1 - large;
    unsigned diff = teams[large].count - teams[small].count;
    unsigned allowed = AllowedDifference(teams[0].count, teams[1].count, options);
    // Scan backwards so earlier queue candidates retain priority.
    for (size_t i = groups.size(); i-- > 0;)
    {
        if (result[i] != int(large)) continue;
        unsigned n = groups[i].size;
        if (n * 2 <= diff + allowed && teams[small].count + n <= options.maximum)
        {
            Apply(teams, groups[i], large, -1);
            Apply(teams, groups[i], small, 1);
            result[i] = int(small);
            return true;
        }
        if (n <= diff + allowed)
        {
            Apply(teams, groups[i], large, -1);
            result[i] = -1;
            return true;
        }
    }
    return false;
}
inline void Repair(std::vector<Group> const& groups, std::vector<int>& result,
    std::array<Team, 2>& teams, Options options)
{
    // Base reservations cannot be removed; live imbalance can remain until
    // enough candidates queue on the smaller side.
    for ([[maybe_unused]] auto const& group : groups)
    {
        unsigned large = teams[0].count > teams[1].count ? 0 : 1;
        unsigned diff = teams[large].count - teams[1 - large].count;
        unsigned allowed = AllowedDifference(teams[0].count, teams[1].count, options);
        if (diff <= allowed || !RepairOne(groups, result, teams, large, options)) return;
    }
}
inline bool AllAssigned(std::vector<Group> const& groups, std::vector<int> const& result)
{
    for (size_t i = 0; i < groups.size(); ++i)
        if (groups[i].size && result[i] < 0) return false;
    return true;
}
inline Score AssignmentScore(std::vector<Group> const& groups, std::vector<int> const& result,
    std::array<Team, 2> const& base, Options options)
{
    std::array<unsigned, 2> counts = {base[0].count, base[1].count};
    unsigned total = 0;
    for (size_t i = 0; i < groups.size(); ++i)
    {
        if (result[i] < 0) continue;
        counts[result[i]] += groups[i].size;
        total += groups[i].size;
    }
    return MakeScore(counts[0], counts[1], total, options);
}
}
// Reachable reservation counts provide a bounded fallback when greedy
// balancing fragments available slots (e.g. 2,3,2,3 in a 5v5 queue).
// A state is recorded once, preserving earlier queue candidates. Parent nodes
// refer only to the preceding iteration, so a group cannot be selected twice.
inline std::vector<int> Pack(std::vector<Group> const& groups, std::array<Team, 2> base, Options options)
{
    Detail::PackingStates states(base, options);
    size_t index = 0;
    for (auto const& group : groups)
    {
        size_t groupIndex = index++;
        if (!group.size || group.nativeTeam > 1) continue;
        states.Add(group, groupIndex);
        if (states.Full()) break;
    }
    return states.Result(groups.size(), options);
}
// -1 leaves a whole group waiting; a premade is never split.
inline std::vector<int> Select(std::vector<Group> const& groups, std::array<Team, 2> teams, Options options)
{
    auto base = teams;
    std::vector result(groups.size(), -1);
    for (size_t i = 0; i < groups.size(); ++i)
    {
        Group const& group = groups[i];
        if (!group.size || group.nativeTeam > 1) continue;
        unsigned team = Detail::PreferredTeam(group, teams, options);
        if (teams[team].count + group.size > options.maximum) team = 1 - team;
        if (teams[team].count + group.size > options.maximum) continue;
        result[i] = int(team);
        Detail::Apply(teams, group, team, 1);
    }
    Detail::Repair(groups, result, teams, options);
    auto score = Detail::AssignmentScore(groups, result, base, options);
    if (!Detail::AllAssigned(groups, result) || !score.acceptable)
    {
        auto packed = Pack(groups, base, options);
        if (Detail::Better(Detail::AssignmentScore(groups, packed, base, options), score)) return packed;
    }
    return result;
}
// Keep the native minority intact. Majority surplus may flip only if it
// improves the live headcount and the destination has a free reservation.
inline unsigned WintergraspTeam(unsigned native, unsigned majority, unsigned fairShare,
    unsigned nativeKept, std::array<unsigned, 2> live, bool nativePriority)
{
    if (nativePriority && (native != majority || nativeKept < fairShare)) return native;
    return live[native] > live[1 - native] ? 1 - native : native;
}
}
#endif
