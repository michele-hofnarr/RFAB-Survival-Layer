#include "PCH.h"

#include "Core/Events.h"
#include "Core/Disease.h"
#include "Core/Forms.h"
#include "Core/RfabPatch.h"
#include "Core/Teleport.h"
#include "Core/Trees.h"
#include "Core/ClimateMap.h"
#include "Core/Needs.h"
#include "Menu/RSLMenu.h"
#include "Settings.h"

namespace
{
    void InitializeLog()
    {
        auto path = logger::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("failed to find the SKSE log directory"sv);
        }
        *path /= "_RSL_Core.log"sv;

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* a_message)
    {
        switch (a_message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            // Everything that needs game data lives behind this point: form
            // lookups, the UI singleton, and the Scaleform manager are all
            // available now and were not at plugin load.
            RSL::Forms::Load();
            RSL::RfabPatch::Apply();
            RSL::Disease::ScanCureEffects();
            RSL::Settings::ReadSettings();
            RSL::RSLMenu::Register();
            RSL::RSLMenu::StartHeartbeat();
            RSL::Trees::GetSingleton().Install();
            RSL::Teleport::Install();
            RSL::Events::Install();
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            // The HUD normally comes up by mirroring HUDMenu, but ask once here
            // too: on a load into an already-open HUD there is no open event to
            // mirror.
            RSL::RSLMenu::Open();
            break;

        default:
            break;
        }
    }
}

// SE-style query. The game here is 1.5.97, which predates the version-data
// struct below, so both have to be present for one binary to load on either.
extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = "RFAB Survival Layer";
    a_info->version = 1;

    if (a_skse->IsEditor()) {
        return false;
    }
    if (a_skse->RuntimeVersion() < SKSE::RUNTIME_SSE_1_5_39) {
        return false;
    }
    return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
    SKSE::PluginVersionData v{};
    v.PluginName("RFAB Survival Layer");
    v.AuthorName("michele-hofnarr");
    v.PluginVersion({ 0, 5, 0, 0 });
    v.UsesAddressLibrary();
    v.UsesNoStructs();
    v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
    return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    InitializeLog();
    logger::info("RFAB Survival Layer native core loading (runtime {})", a_skse->RuntimeVersion().string());

    SKSE::Init(a_skse);

    RSL::ClimateMap::Load();
    RSL::Needs::InstallSerialization();

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener("SKSE", MessageHandler)) {
        logger::error("could not register the SKSE message listener");
        return false;
    }

    return true;
}
