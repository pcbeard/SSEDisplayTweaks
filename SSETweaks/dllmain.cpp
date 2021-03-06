#include "pch.h"

namespace SDT {
    constexpr char* CONF_SECT_MAIN = "Main";
    constexpr char* CONF_MAIN_KEY_LOGGING = "LogLevel";

    static bool Initialize(const SKSEInterface* skse)
    {
        int result = IConfig::Load();
        if (result != 0) {
            gLogger.Warning(
                "WARNING: Unable to load the configuration file (%d)", result);
        }
        else {
            SDT::IConfig conf;
            gLogger.SetLogLevel(Logger::TranslateLogLevel(
                conf.GetConfigValue(CONF_SECT_MAIN, CONF_MAIN_KEY_LOGGING, "debug")));
        }

        if (!ISKSE::Initialize(skse)) {
            return false;
        }

        if (!IEvents::Initialize()) {
            return false;
        }

        if (!IDDispatcher::InitializeDrivers()) {
            return false;
        }

        gLogger.Debug("[Trampoline] branch: %zu/%zu, codegen: %zu/%u",
            ISKSE::branchTrampolineSize - g_branchTrampoline.Remain(),
            ISKSE::branchTrampolineSize,
            ISKSE::localTrampolineSize - g_localTrampoline.Remain(),
            ISKSE::localTrampolineSize);

        return true;
    }
}

extern "C"
{
    bool SKSEPlugin_Query(const SKSEInterface* skse, PluginInfo* info)
    {
        return SDT::ISKSE::Query(skse, info);
    }

    bool SKSEPlugin_Load(const SKSEInterface* skse)
    {
        _assert(SDT::ISKSE::g_moduleHandle != nullptr);

        gLogger.Message("%s version %s (runtime %u.%u.%u.%u)",
            PLUGIN_NAME, PLUGIN_VERSION_VERSTRING,
            GET_EXE_VERSION_MAJOR(skse->runtimeVersion),
            GET_EXE_VERSION_MINOR(skse->runtimeVersion),
            GET_EXE_VERSION_BUILD(skse->runtimeVersion),
            GET_EXE_VERSION_SUB(skse->runtimeVersion));

        if (!IAL::IsLoaded()) {
            gLogger.FatalError("Could not load the address library, check requirements on the nexus page");
            return false;
        }

        if (IAL::HasBadQuery()) {
            gLogger.FatalError("One or more addresses could not be retrieved from the database");
            return false;
        }

        PerfTimer timer;

        timer.Start();

        bool result = SDT::Initialize(skse);

        auto tInit = timer.Stop();

        timer.Start();

        IAL::Release();

        auto tUnload = timer.Stop();

        if (result) {
            gLogger.Debug("[%s] db load: %.3f ms, init: %.3f ms, db unload: %.3f ms", __FUNCTION__,
                IAL::GetLoadTime() * 1000.0f, tInit * 1000.0f, tUnload * 1000.0f);
        }

        return result;
    }
};

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        SDT::ISKSE::g_moduleHandle = hModule;
        break;
    }
    return TRUE;
}