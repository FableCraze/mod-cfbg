/* Copyright since 2019 AzerothCore contributors. MIT; see ../LICENSE. */
#ifndef CFBG_SCRIPT_LIFETIME_H
#define CFBG_SCRIPT_LIFETIME_H
#include <memory>

template<class Script>
class CFBGScriptLifetime
{
public:
    static void Create()
    {
        if (!Handle())
            Handle() = std::make_unique<Script>();
    }

    static void Destroyed(Script const* script)
    {
        // ScriptMgr deletes registered scripts during Unload. Relinquish the
        // matching handle from that destructor so there is no second delete.
        if (Handle().get() == script)
            Handle().release();
    }

private:
    static std::unique_ptr<Script>& Handle()
    {
        static std::unique_ptr<Script> script;
        return script;
    }
};
#endif
