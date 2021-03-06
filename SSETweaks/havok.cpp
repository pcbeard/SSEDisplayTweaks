#include "pch.h"

namespace SDT
{
    constexpr char* SECTION_GENERAL = "General";
    constexpr char* SECTION_HAVOK = "Havok";

    constexpr char* CKEY_HAVOKON = "DynamicMaxTimeScaling";
    constexpr char* CKEY_HAVOKENABLED = "Enabled";
    constexpr char* CKEY_MAXFPS = "MaximumFramerate";
    constexpr char* CKEY_MINFPS = "MinimumFramerate";
    constexpr char* CKEY_FMTCOFFSET = "MaxTimeComplexOffset";
    constexpr char* CKEY_STATSON = "OSDStatsEnabled";

    constexpr char* CKEY_ADJUSTINICFG = "AdjustGameSettings";

    DHavok DHavok::m_Instance;

    DHavok::DHavok()
    {
        m_Instance.bufStats[0] = 0x0;
    }

    void DHavok::LoadConfig()
    {
        conf.havok_enabled = GetConfigValue(SECTION_HAVOK, CKEY_HAVOKENABLED, false);
        conf.havok_on = GetConfigValue(SECTION_HAVOK, CKEY_HAVOKON, true);
        conf.fmt_min = GetConfigValue(SECTION_HAVOK, CKEY_MINFPS, 60.0f);
        conf.fmt_max = GetConfigValue(SECTION_HAVOK, CKEY_MAXFPS, 0.0f);
        conf.fmtc_offset = std::clamp(GetConfigValue(SECTION_HAVOK, CKEY_FMTCOFFSET, 30.0f), 0.0f, 30.0f);
        conf.stats_enabled = GetConfigValue(SECTION_HAVOK, CKEY_STATSON, false);
        conf.adjust_ini = GetConfigValue(SECTION_GENERAL, CKEY_ADJUSTINICFG, true);
    }

    void DHavok::PostLoadConfig()
    {
        if (conf.havok_enabled)
        {
            auto rd = IDDispatcher::GetDriver<DRender>(DRIVER_RENDER);

            if (conf.havok_on && !rd || !rd->IsOK()) {
                conf.havok_on = false;
                Error("Render driver unavailable, disabling dynamic scaling");
            }

            if (conf.fmt_max > 0.0f) {
                if (conf.fmt_max < HAVOK_MAXTIME_MIN) {
                    conf.fmt_max = HAVOK_MAXTIME_MIN;
                }

                fmt_min = 1.0f / conf.fmt_max;
            }

            if (conf.fmt_min < HAVOK_MAXTIME_MIN) {
                conf.fmt_min = HAVOK_MAXTIME_MIN;
            }

            fmt_max = 1.0f / conf.fmt_min;

            if (conf.fmt_max > 0.0f)
            {
                if (conf.fmt_min == conf.fmt_max) {
                    conf.havok_on = false;
                }
            }
        }
    }

    void DHavok::RegisterHooks()
    {
        if (conf.havok_enabled) {
            IEvents::RegisterForEvent(Event::OnD3D11PreCreate, OnD3D11PreCreate_Havok);

            if (conf.havok_on) {

                OSDDriver = IDDispatcher::GetDriver<DOSD>(DRIVER_OSD);

                bool regOSDEvent;

                uintptr_t hf;
                if (OSDDriver && OSDDriver->IsOK() && OSDDriver->conf.enabled && conf.stats_enabled)
                {
                    regOSDEvent = true;
                    hf = reinterpret_cast<uintptr_t>(hookRTHStats);
                }
                else {
                    regOSDEvent = false;
                    hf = reinterpret_cast<uintptr_t>(hookRTH);
                }

                if (!Hook::Call5(PhysCalcMaxTime, hf, PhysCalcMaxTime_O))
                {
                    conf.havok_on = false;
                    Error("Couldn't hook physics calc function");
                    return;
                }

                if (regOSDEvent) {
                    IEvents::RegisterForEvent(Event::OnD3D11PostCreate, OnD3D11PostCreate_Havok);
                }
            }
        }
    }

    bool DHavok::Prepare()
    {
        fMaxTime = ISKSE::GetINISettingAddr<float>("fMaxTime:HAVOK");
        if (!fMaxTime) {
            return false;
        }

        fMaxTimeComplex = ISKSE::GetINISettingAddr<float>("fMaxTimeComplex:HAVOK");
        if (!fMaxTimeComplex) {
            return false;
        }

        uMaxNumPhysicsStepsPerUpdate = ISKSE::GetINISettingAddr<uint32_t>("uMaxNumPhysicsStepsPerUpdate:HAVOK");
        if (!uMaxNumPhysicsStepsPerUpdate) {
            return false;
        }

        uMaxNumPhysicsStepsPerUpdateComplex = ISKSE::GetINISettingAddr<uint32_t>("uMaxNumPhysicsStepsPerUpdateComplex:HAVOK");
        if (!uMaxNumPhysicsStepsPerUpdateComplex) {
            return false;
        }

        return true;
    }

    void DHavok::CalculateHavokValues(bool a_isComplex)
    {
        float interval = std::clamp(*Game::frameTimer, fmt_min, fmt_max);

        *fMaxTime = interval;

        if (a_isComplex)
            *fMaxTimeComplex = 1.0f / max(1.0f / interval - conf.fmtc_offset, HAVOK_MAXTIME_MIN);
    }

    void DHavok::UpdateHavokStats()
    {
        IStats::Accum(10, double(*fMaxTime));
        IStats::Accum(11, double(*fMaxTimeComplex));
    }

    float DHavok::AutoGetMaxTime(const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, float def)
    {
        auto rd = IDDispatcher::GetDriver<DRender>(DRIVER_RENDER);

        float maxt = rd->GetMaxFramerate(pSwapChainDesc);
        if (maxt > 0.0f) {
            maxt = 1.0f / maxt;
        }
        else {
            maxt = 1.0f / def;
            Warning("Unable to calculate optimal fMaxTime, using %.6g", maxt);
        }

        return maxt;
    }

    bool DHavok::HavokHasPossibleIssues(const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, float t)
    {
        auto rd = IDDispatcher::GetDriver<DRender>(DRIVER_RENDER);

        float maxfr = rd->GetMaxFramerate(pSwapChainDesc);
        if (maxfr > 0.0f && t > 1.0f / maxfr) {
            return true;
        }
        return false;
    }

    void DHavok::ApplyHavokSettings(DXGI_SWAP_CHAIN_DESC* pSwapChainDesc)
    {
        float maxt;

        if (conf.havok_on) {
            if (conf.fmt_max <= 0.0f) {
                fmt_min = AutoGetMaxTime(pSwapChainDesc, 240.0f);
            }

            if (fmt_min > fmt_max) {
                fmt_max = fmt_min;
            }

            if (fmt_max == fmt_min) {
                Warning("Dynamic fMaxTime scaling is enabled but MinimumFramerate is equal to MaximumFramerate. Adjust your configuration.");
            }

            maxt = fmt_min;

            Message("(DYNAMIC) fMaxTime=%.6g-%.6g fMaxTimeComplexOffset=%.6g (Max FPS = %.6g)",
                fmt_min, fmt_max, conf.fmtc_offset, 1.0f / fmt_min);
        }
        else {
            if (conf.fmt_max > 0.0f) {
                maxt = fmt_min;
            }
            else {
                maxt = AutoGetMaxTime(pSwapChainDesc, 60.0f);
            }

            float maxtc = 1.0f / max(1.0f / maxt - conf.fmtc_offset, HAVOK_MAXTIME_MIN);

            *fMaxTime = maxt;
            *fMaxTimeComplex = maxtc;

            Message("(STATIC) fMaxTime=%.6g fMaxTimeComplex=%.6g (Max FPS = %.6g)", maxt, maxtc, 1.0f / maxt);
        }

        if (HavokHasPossibleIssues(pSwapChainDesc, maxt)) {
            Warning("With the current configuration frame times could fall below fMaxTime. You may experience physics issues.");
        }

        if (!*uMaxNumPhysicsStepsPerUpdate) {
            *uMaxNumPhysicsStepsPerUpdate = 3;
            Warning("uMaxNumPhysicsStepsPerUpdate is 0, adjusting to 3");
        }
        else if (*uMaxNumPhysicsStepsPerUpdate != 3) {
            if (conf.adjust_ini) {
                m_Instance.Message("Setting uMaxNumPhysicsStepsPerUpdate=3");
                *uMaxNumPhysicsStepsPerUpdate = 3;
            }
            else {
                Warning("uMaxNumPhysicsStepsPerUpdate != 3, recommend resetting to default");
            }
        }

        if (!*uMaxNumPhysicsStepsPerUpdateComplex) {
            *uMaxNumPhysicsStepsPerUpdateComplex = 1;
            Warning("uMaxNumPhysicsStepsPerUpdateComplex is 0, adjusting to 1");
        }
        else if (*uMaxNumPhysicsStepsPerUpdateComplex != 1) {
            if (conf.adjust_ini) {
                m_Instance.Message("Setting uMaxNumPhysicsStepsPerUpdateComplex=1");
                *uMaxNumPhysicsStepsPerUpdateComplex = 1;
            }
            else {
                Warning("uMaxNumPhysicsStepsPerUpdateComplex != 1, recommend resetting to default");
            }
        }
    }

    void DHavok::hookRTH(float a_time, bool a_isComplex, uint8_t a_unk0)
    {
        m_Instance.CalculateHavokValues(a_isComplex);
        m_Instance.PhysCalcMaxTime_O(a_time, a_isComplex, a_unk0);
    }

    void DHavok::hookRTHStats(float a_time, bool a_isComplex, uint8_t a_unk0)
    {
        m_Instance.CalculateHavokValues(a_isComplex);
        m_Instance.UpdateHavokStats();
        m_Instance.PhysCalcMaxTime_O(a_time, a_isComplex, a_unk0);
    }

    const wchar_t* DHavok::StatsRendererCallback()
    {
        double val;

        if (!*m_Instance.isComplex)
        {
            if (IStats::Get(10, val)) {
                _snwprintf_s(m_Instance.bufStats, _TRUNCATE,
                    L"fMaxTime: %.4g", val);
            }
        }
        else
        {
            if (IStats::Get(11, val)) {
                _snwprintf_s(m_Instance.bufStats, _TRUNCATE,
                    L"fMaxTimeComplex: %.4g", val);
            }
        }

        return m_Instance.bufStats;
    }

    void DHavok::OnD3D11PreCreate_Havok(Event code, void* data)
    {
        auto info = reinterpret_cast<D3D11CreateEventPre*>(data);

        m_Instance.ApplyHavokSettings(const_cast<DXGI_SWAP_CHAIN_DESC*>(info->m_pSwapChainDesc));
    }

    void DHavok::OnD3D11PostCreate_Havok(Event code, void* data)
    {
        m_Instance.OSDDriver->AddStatsCallback(StatsRendererCallback);
    }
}
