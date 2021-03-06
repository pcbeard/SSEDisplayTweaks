#include "pch.h"

namespace SDT
{
    IDriver::IDriver() :
        m_Initialized(false),
        m_IsOK(false)
    {
        IDDispatcher::RegisterDriver(this);
    };

    bool IDriver::Initialize()
    {
        if (m_Initialized) {
            return false;
        }

        LoadConfig();
        PostLoadConfig();
        RegisterHooks();

        if (IsOK()) {
            if (InstallHooks()) { // can't softfail atm
                Patch();
            }
            else {
                SetOK(false);
            }
        }

        m_Initialized = true;

        return true;
    };


    void IHook::RegisterHook(uintptr_t target, uintptr_t hook)
    {
        m_hooks.push_back(HookDescriptor(target, hook, HookDescriptor::HookType::kWR5Call));
    }

    void IHook::RegisterHook(uintptr_t target, uintptr_t hook, HookDescriptor::HookType type)
    {
        m_hooks.push_back(HookDescriptor(target, hook, type));
    }

    void IHook::RegisterHook(const HookDescriptor& hdesc)
    {
        m_hooks.push_back(hdesc);
    }

    void IHook::RegisterHook(HookDescriptor&& hdesc)
    {
        m_hooks.emplace_back(std::forward<HookDescriptor>(hdesc));
    }

    bool IHook::InstallHooks()
    {
        if (!m_hooks.size()) {
            return true;
        }

        uint32_t c = 0;

        for (const auto& hdesc : m_hooks) {
            if (hdesc.type == HookDescriptor::kWR5Call) {
                Debug("BranchTrampoline::Write5Call %llX -> %llX", hdesc.wc_target, hdesc.wc_hook);
                g_branchTrampoline.Write5Call(hdesc.wc_target, hdesc.wc_hook);
                c++;
            }
            else if (hdesc.type == HookDescriptor::kWR6Call) {
                Debug("BranchTrampoline::Write6Call %llX -> %llX", hdesc.wc_target, hdesc.wc_hook);
                g_branchTrampoline.Write6Call(hdesc.wc_target, hdesc.wc_hook);
                c++;
            }
        }

        Debug("%u hook(s) installed", c);

        return true;
    }
}