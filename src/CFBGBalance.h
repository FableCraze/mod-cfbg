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

// Reachable reservation counts provide a bounded fallback when greedy
// balancing fragments the available slots (e.g. 2,3,2,3 in a 5v5 queue).
// A state is recorded once, preserving earlier queue candidates. Parent nodes
// refer only to the preceding iteration, so a group cannot be selected twice.
inline std::vector<int> Pack(std::vector<Group> const& groups, std::array<Team,2> base, Options options)
{
    unsigned capA = base[0].count >= options.maximum ? 0 : options.maximum-base[0].count;
    unsigned capH = base[1].count >= options.maximum ? 0 : options.maximum-base[1].count;
    unsigned stride = capH+1;
    struct Node { int parent; size_t group; int team; };
    std::vector<Node> nodes(1,{-1,0,-1});
    std::vector<int> states((capA+1)*stride,-1); states[0]=0;
    for(size_t i=0;i<groups.size();++i)
    {
        unsigned n=groups[i].size;
        if(!n || groups[i].nativeTeam>1) continue;
        auto next=states;
        for(unsigned a=0;a<=capA;++a)
            for(unsigned h=0;h<=capH;++h)
            {
                int parent=states[a*stride+h]; if(parent<0) continue;
                unsigned preferred=groups[i].nativeTeam;
                if(base[0].count+a != base[1].count+h) preferred=base[0].count+a < base[1].count+h ? 0 : 1;
                for(unsigned pass=0;pass<2;++pass)
                {
                    unsigned t=pass ? 1-preferred : preferred;
                    unsigned aa=a+(t==0?n:0), hh=h+(t==1?n:0);
                    if(aa>capA || hh>capH || next[aa*stride+hh]>=0) continue;
                    next[aa*stride+hh]=int(nodes.size());
                    nodes.push_back({parent,i,int(t)});
                }
            }
        states.swap(next);
        if(states[capA*stride+capH]>=0) break;
    }
    bool bestAcceptable=false; unsigned bestTotal=0,bestDiff=~0u; int best=0;
    for(unsigned a=0;a<=capA;++a)
        for(unsigned h=0;h<=capH;++h)
        {
            int state=states[a*stride+h]; if(state<0) continue;
            unsigned ca=base[0].count+a,ch=base[1].count+h;
            unsigned diff=ca>ch?ca-ch:ch-ca;
            unsigned allowed=options.relaxAtCount && std::min(ca,ch)>=options.relaxAtCount ? 1 : options.allowedDifference;
            bool acceptable=diff<=allowed;
            if((acceptable && !bestAcceptable) || (acceptable==bestAcceptable &&
                (acceptable ? (a+h>bestTotal || (a+h==bestTotal && diff<bestDiff)) : (diff<bestDiff || (diff==bestDiff && a+h>bestTotal)))))
            {
                bestAcceptable=acceptable;bestTotal=a+h;bestDiff=diff;best=state;
            }
        }
    std::vector<int> result(groups.size(),-1);
    for(int node=best;node>0;node=nodes[node].parent) result[nodes[node].group]=nodes[node].team;
    return result;
}

// -1 leaves a whole group waiting; a premade is never split.
inline std::vector<int> Select(std::vector<Group> const& groups, std::array<Team, 2> teams, Options options)
{
    auto base=teams;
    std::vector<int> result(groups.size(), -1);
    auto apply = [&](Group const& g, unsigned t, int sign)
    {
        if (sign > 0) teams[t].count += g.size;
        else teams[t].count -= g.size;
        teams[t].levels += sign * g.level * g.size;
        teams[t].items += sign * g.ilvl * g.size;
        if (sign > 0) teams[t].hunters += g.hunters;
        else teams[t].hunters -= g.hunters;
    };
    for (size_t i = 0; i < groups.size(); ++i)
    {
        Group const& g = groups[i];
        if (!g.size || g.nativeTeam > 1) continue;
        unsigned t = g.nativeTeam;
        if (teams[0].count != teams[1].count)
            t = teams[0].count < teams[1].count ? 0 : 1;
        else if (options.levels && teams[0].levels != teams[1].levels)
            t = teams[0].levels < teams[1].levels ? 0 : 1;
        else if ((options.items || options.levels) && teams[0].items != teams[1].items)
            t = teams[0].items < teams[1].items ? 0 : 1;
        else if (options.hunters && teams[0].hunters != teams[1].hunters)
            t = teams[0].hunters < teams[1].hunters ? 0 : 1;
        if (teams[t].count + g.size > options.maximum)
            t = 1 - t;
        if (teams[t].count + g.size > options.maximum) continue;
        result[i] = int(t);
        apply(g, t, 1);
    }
    // Repair the projected difference by moving or deferring whole groups.
    // Base reservations cannot be removed, so an already uneven live BG may
    // remain uneven until enough candidates queue on the smaller side.
    for (size_t pass = 0; pass < groups.size(); ++pass)
    {
        unsigned large = teams[0].count > teams[1].count ? 0 : 1;
        unsigned small = 1 - large;
        unsigned diff = teams[large].count - teams[small].count;
        unsigned allowed = options.relaxAtCount && std::min(teams[0].count,teams[1].count) >= options.relaxAtCount ? 1 : options.allowedDifference;
        if (diff <= allowed) break;
        bool changed = false;
        for (size_t i = groups.size(); i-- > 0;)
        {
            if (result[i] != int(large)) continue;
            unsigned n = groups[i].size;
            if (n * 2 <= diff + allowed && teams[small].count + n <= options.maximum)
            {
                apply(groups[i], large, -1);
                apply(groups[i], small, 1);
                result[i] = int(small);
                changed = true;
                break;
            }
            if (n <= diff + allowed)
            {
                apply(groups[i], large, -1);
                result[i] = -1;
                changed = true;
                break;
            }
        }
        if (!changed) break;
    }
    bool allAssigned=true;
    for(size_t i=0;i<groups.size();++i) if(groups[i].size && result[i]<0) allAssigned=false;
    unsigned finalDiff=teams[0].count>teams[1].count?teams[0].count-teams[1].count:teams[1].count-teams[0].count;
    unsigned finalAllowed=options.relaxAtCount && std::min(teams[0].count,teams[1].count)>=options.relaxAtCount ? 1 : options.allowedDifference;
    if(!allAssigned || finalDiff>finalAllowed)
    {
        auto packed=Pack(groups,base,options);
        unsigned packedTotal=0,greedyTotal=0;
        std::array<unsigned,2> pc={{base[0].count,base[1].count}},gc=pc;
        for(size_t i=0;i<groups.size();++i)
        {
            if(packed[i]>=0){pc[packed[i]]+=groups[i].size;packedTotal+=groups[i].size;}
            if(result[i]>=0){gc[result[i]]+=groups[i].size;greedyTotal+=groups[i].size;}
        }
        unsigned pd=pc[0]>pc[1]?pc[0]-pc[1]:pc[1]-pc[0];
        unsigned gd=gc[0]>gc[1]?gc[0]-gc[1]:gc[1]-gc[0];
        unsigned pa=options.relaxAtCount && std::min(pc[0],pc[1])>=options.relaxAtCount ? 1 : options.allowedDifference;
        unsigned ga=options.relaxAtCount && std::min(gc[0],gc[1])>=options.relaxAtCount ? 1 : options.allowedDifference;
        bool pok=pd<=pa,gok=gd<=ga;
        if((pok && !gok) || (pok==gok && (pok ? (packedTotal>greedyTotal || (packedTotal==greedyTotal && pd<gd)) : (pd<gd || (pd==gd && packedTotal>greedyTotal))))) return packed;
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
