#include "CortexCoreModule.h"
#include "CortexCommandRouter.h"
#include "CortexCoreCommandHandler.h"
#include "CortexTcpServer.h"
#include "CortexHttpServer.h"
#include "CortexSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY(LogCortex);

#if WITH_EDITORONLY_DATA && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
namespace CortexCoreGarbageCollection
{
// UE 5.4 reports UClass::ClassGeneratedBy to the garbage collector twice per pass: as a declared
// intrinsic member (UCLASS_EDITOR_GC_MEMBERS in Class.cpp) and from UClass::AddReferencedObjects.
// When the generating Blueprint is garbage while its class is still reachable, both visits
// eliminate (null) the same slot, possibly on two GC threads at once; the visit that validated the
// slot as non-null then queues a null reference and crashes in TReferenceBatcher::DrainValidated
// (EXCEPTION_ACCESS_VIOLATION reading 0xc). Clearing such links on the game thread before
// reachability starts gives the result garbage elimination produces in this same pass, minus the race.
static FDelegateHandle PreGarbageCollectHandle;

static void ClearGarbageClassGenerators()
{
    if (!UObject::IsGarbageEliminationEnabled())
    {
        return;
    }
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    for (TObjectIterator<UClass> It; It; ++It)
    {
        UClass* Class = *It;
        const UObject* Generator = Class->ClassGeneratedBy;
        if (Generator != nullptr && Generator->HasAnyInternalFlags(EInternalObjectFlags::Garbage))
        {
            Class->ClassGeneratedBy = nullptr;
        }
    }
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
}
}
#endif

// POC: in-editor HTTP/SSE MCP transport. Port > 0 enables it alongside the TCP server.
// Defaults ON (8127) on this isolated POC branch; production would default to 0 (off).
static TAutoConsoleVariable<int32> CVarCortexHttpPort(
	TEXT("cortex.http.port"),
	8127,
	TEXT("Port for the experimental in-editor MCP-over-HTTP server (0 = disabled)."),
	ECVF_Default);

void FCortexCoreModule::StartupModule()
{
    UE_LOG(LogCortex, Log, TEXT("CortexCore module starting up"));

#if WITH_EDITORONLY_DATA && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    CortexCoreGarbageCollection::PreGarbageCollectHandle = FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(
        &CortexCoreGarbageCollection::ClearGarbageClassGenerators);
#endif

    const UCortexSettings* Settings = UCortexSettings::Get();
    if (Settings == nullptr || !Settings->bAutoStart)
    {
        return;
    }

    InstanceId = FGuid::NewGuid().ToString();

    CommandRouter = MakeUnique<FCortexCommandRouter>();

    TcpServer = MakeUnique<FCortexTcpServer>();
    TcpServer->Start(Settings->Port,
        [this](const FString& Command, const TSharedPtr<FJsonObject>& Params, FDeferredResponseCallback DeferredCallback)
        {
            return CommandRouter->Execute(Command, Params, MoveTemp(DeferredCallback));
        }
    );

    UE_LOG(LogCortex, Log, TEXT("CortexCore TCP server started on port %d"), Settings->Port);

    CommandRouter->RegisterDomain(
        TEXT("core"),
        TEXT("Cortex Core"),
        TEXT("1.0.1"),
        MakeShared<FCortexCoreCommandHandler>(CommandRouter.Get())
    );

    // POC: optional MCP-over-HTTP transport alongside the TCP server.
    const int32 HttpPort = CVarCortexHttpPort.GetValueOnGameThread();
    if (HttpPort > 0)
    {
        HttpServer = MakeUnique<FCortexHttpServer>();
        if (!HttpServer->Start(HttpPort, *CommandRouter))
        {
            HttpServer.Reset();
        }
    }
}

void FCortexCoreModule::ShutdownModule()
{
    UE_LOG(LogCortex, Log, TEXT("CortexCore module shutting down"));

#if WITH_EDITORONLY_DATA && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    FCoreUObjectDelegates::GetPreGarbageCollectDelegate().Remove(CortexCoreGarbageCollection::PreGarbageCollectHandle);
    CortexCoreGarbageCollection::PreGarbageCollectHandle.Reset();
#endif

    if (HttpServer.IsValid())
    {
        HttpServer->Stop();
        HttpServer.Reset();
    }

    if (TcpServer.IsValid())
    {
        TcpServer->Stop();
        TcpServer.Reset();
    }

    CommandRouter.Reset();
}

ICortexCommandRegistry& FCortexCoreModule::GetCommandRegistry()
{
    check(CommandRouter.IsValid());
    return *CommandRouter; // FCortexCommandRouter implements ICortexCommandRegistry
}

FCortexCommandRouter& FCortexCoreModule::GetCommandRouter()
{
    check(CommandRouter.IsValid());
    return *CommandRouter;
}

void FCortexCoreModule::RequestSerialization(const FCortexSerializationRequest& Request, FOnSerializationComplete Callback)
{
	if (SerializationHandler.IsBound())
	{
		SerializationHandler.Execute(Request, Callback);
	}
	else
	{
		UE_LOG(LogCortex, Log, TEXT("RequestSerialization called but no handler bound"));
		FCortexSerializationResult ErrorResult;
		ErrorResult.bSuccess = false;
		ErrorResult.JsonPayload = TEXT("{\"error\":\"No serialization handler bound\"}");
		Callback.Execute(ErrorResult);
	}
}

bool FCortexCoreModule::IsServerRunning() const
{
    return TcpServer.IsValid() && TcpServer->IsRunning();
}

int32 FCortexCoreModule::GetServerPort() const
{
    return TcpServer.IsValid() ? TcpServer->GetBoundPort() : 0;
}

int32 FCortexCoreModule::GetClientCount() const
{
    return TcpServer.IsValid() ? TcpServer->GetClientCount() : 0;
}

int32 FCortexCoreModule::GetDomainCount() const
{
    return CommandRouter.IsValid() ? CommandRouter->GetRegisteredDomains().Num() : 0;
}

void FCortexCoreModule::SetClientDisconnectCallback(FCortexTcpServer::FClientDisconnectCallback Callback)
{
    if (TcpServer.IsValid())
    {
        TcpServer->SetClientDisconnectCallback(MoveTemp(Callback));
    }
    else
    {
        UE_LOG(LogCortex, Warning, TEXT("SetClientDisconnectCallback: TcpServer not ready — callback dropped"));
    }
}

IMPLEMENT_MODULE(FCortexCoreModule, CortexCore)
