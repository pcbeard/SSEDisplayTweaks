#pragma once

namespace SDT
{
    constexpr size_t MAX_TRAMPOLINE_BRANCH = 256;
    constexpr size_t MAX_TRAMPOLINE_CODEGEN = 384;

    class ISKSE
    {
    public:
        static bool Query(const SKSEInterface* skse, PluginInfo* info);
        static bool Initialize(const SKSEInterface* skse);

        template <typename T>
        [[nodiscard]] static T* GetINISettingAddr(const char* name)
        {
            Setting* setting = (*g_iniSettingCollection)->Get(name);
            if (setting) {
                return reinterpret_cast<T*>(&setting->data);
            }
            return NULL;
        };

        template <typename T>
        [[nodiscard]] static T* GetINIPrefSettingAddr(const char* name)
        {
            Setting* setting = (*g_iniPrefSettingCollection)->Get(name);
            if (setting) {
                return reinterpret_cast<T*>(&setting->data);
            }
            return nullptr;
        };

        static HMODULE g_moduleHandle;
        static PluginHandle g_pluginHandle;

        static SKSEMessagingInterface* g_messaging;

        static size_t branchTrampolineSize;
        static size_t localTrampolineSize;
    protected:
        ISKSE() = default;
    };
}