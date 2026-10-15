#include "CortexUMGModule.h"
#include "CortexCoreModule.h"
#include "ICortexCommandRegistry.h"
#include "CortexUMGCommandHandler.h"
#include "CortexUMGUtils.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
#include "CortexLegacyWidgetIdentity.h"
#endif

DEFINE_LOG_CATEGORY(LogCortexUMG);

void FCortexUMGModule::StartupModule()
{
    UE_LOG(LogCortexUMG, Log, TEXT("CortexUMG module starting up"));

    ICortexCommandRegistry& Registry =
        FModuleManager::GetModuleChecked<FCortexCoreModule>(TEXT("CortexCore"))
        .GetCommandRegistry();

    Registry.RegisterDomain(
        TEXT("umg"),
        TEXT("Cortex UMG"),
        TEXT("1.0.1"),
        MakeShared<FCortexUMGCommandHandler>()
    );

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    // UE 5.4/5.5 keep Designer identities in a Blueprint extension the native compiler does not
    // know; seed an empty one on compile the way the 5.6+ compiler seeds its own empty map.
    UCortexLegacyWidgetIdentity::SeedEmptyMapOnCompile = [](UBlueprint& OwningBlueprint, TMap<FName, FGuid>& Guids)
    {
        if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(&OwningBlueprint))
        {
            CortexUMGUtils::SeedDeterministicIdentities(WidgetBlueprint, Guids);
        }
    };
#endif

    UE_LOG(LogCortexUMG, Log, TEXT("CortexUMG registered with CortexCore"));
}

void FCortexUMGModule::ShutdownModule()
{
    UE_LOG(LogCortexUMG, Log, TEXT("CortexUMG module shutting down"));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    UCortexLegacyWidgetIdentity::SeedEmptyMapOnCompile = nullptr;
#endif
}

IMPLEMENT_MODULE(FCortexUMGModule, CortexUMG)
