#include "Operations/CortexGraphPatchOps.h"
#include "Operations/CortexGraphPatchState.h"
#include "Misc/AutomationTest.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "CortexGraphMigrationTestTypes.h"
#include "CortexGraphTestContentRoot.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/ComponentDelegateBinding.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "K2Node_Composite.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS
namespace CortexGraphMigrationRetireTest
{
struct FFixture
{
	UPackage* Package = nullptr;
	UWidgetBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UK2Node_Event* Alpha = nullptr;
	UK2Node_Event* Beta = nullptr;
	UK2Node_Event* Retained = nullptr;
	UK2Node_CallFunction* Producer = nullptr;
	UK2Node_CallFunction* AlphaBody = nullptr;
	UK2Node_CallFunction* BetaBody = nullptr;
	UK2Node_CallFunction* RetainedBody = nullptr;
	FGuid AlphaGuid;
	FGuid BetaGuid;
	FGuid RetainedGuid;
	FGuid ProducerGuid;
	FGuid AlphaBodyGuid;
	FGuid BetaBodyGuid;
	FGuid RetainedBodyGuid;
	/** Late-retained producer chain: the execution body whose output feeds the retained cosmetic body. */
	UK2Node_CallFunction* RetainedProducer = nullptr;
	/** The execution body downstream of `RetainedProducer`, which must never be approved for removal. */
	UK2Node_CallFunction* RetainedProducerConsumer = nullptr;
	/** The pair of pure producers whose outputs feed each other inside the selected island. */
	UK2Node_CallFunction* CycleFirst = nullptr;
	UK2Node_CallFunction* CycleSecond = nullptr;
	FGuid RetainedProducerGuid;
	FGuid RetainedProducerConsumerGuid;
	FGuid CycleFirstGuid;
	FGuid CycleSecondGuid;


	UK2Node_Event* AddEvent(const TCHAR* Name)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_Event* Existing = Cast<UK2Node_Event>(Node);
			if (Existing && Existing->EventReference.GetMemberName() == FName(Name))
			{
				Existing->EventReference.SetExternalMember(FName(Name), UCortexGraphRetireLegacyWidget::StaticClass());
				Existing->bOverrideFunction = true;
				return Existing;
			}
		}
		UK2Node_Event* Event = NewObject<UK2Node_Event>(Graph);
		Event->EventReference.SetExternalMember(FName(Name), UCortexGraphRetireLegacyWidget::StaticClass());
		Event->bOverrideFunction = true;
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		Graph->AddNode(Event, true, false);
		return Event;
	}

	UK2Node_CallFunction* AddCall(UFunction* Function)
	{
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph);
		Call->FunctionReference.SetExternalMember(Function->GetFName(), Function->GetOuterUClass());
		Call->CreateNewGuid();
		Call->AllocateDefaultPins();
		Graph->AddNode(Call, true, false);
		return Call;
	}
	UK2Node_CustomEvent* AddNativeNameCollision()
	{
		UK2Node_CustomEvent* Event = NewObject<UK2Node_CustomEvent>(Graph);
		Event->CustomFunctionName = TEXT("OnInitialized");
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		Graph->AddNode(Event, true, false);
		return Event;
	}


	bool Build(const TCHAR* Name, const bool bRetainProducer = false, const bool bBlockAlpha = false,
		UClass* ParentClass = nullptr, const bool bCompileParentFirst = false, const bool bDelayGraphNodes = false)
	{
		EnsureCortexGraphTestTempContentRoot();
		Package = CreatePackage(*FString::Printf(TEXT("/Game/Temp/%s"), Name));
		Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			ParentClass ? ParentClass : UCortexGraphRetireLegacyWidget::StaticClass(), Package, FName(Name), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		if (!Blueprint) return false;
		if (Blueprint->UbergraphPages.Num() == 0)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, UEdGraphSchema_K2::GN_EventGraph,
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
		}
		else Graph = Blueprint->UbergraphPages[0];
		if (bCompileParentFirst) FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return bDelayGraphNodes || PopulateGraph(bRetainProducer, bBlockAlpha);
	}

	bool PopulateGraph(const bool bRetainProducer = false, const bool bBlockAlpha = false)
	{
		Alpha = AddEvent(TEXT("OnLegacyAlpha"));
		Beta = AddEvent(TEXT("OnLegacyBeta"));
		Retained = AddEvent(TEXT("OnRetainedEvent"));
		Producer = AddCall(UKismetStringLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_IntToString")));
		AlphaBody = AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
		BetaBody = AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
		RetainedBody = AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
		const UEdGraphSchema* Schema = Graph->GetSchema();
		auto Link = [Schema](UEdGraphNode* From, const TCHAR* FromName, UEdGraphNode* To, const TCHAR* ToName)
		{
			UEdGraphPin* FromPin = From ? From->FindPin(FName(FromName)) : nullptr;
			UEdGraphPin* ToPin = To ? To->FindPin(FName(ToName)) : nullptr;
			return Schema && FromPin && ToPin && Schema->TryCreateConnection(FromPin, ToPin);
		};
		if (!Link(Alpha, TEXT("then"), AlphaBody, TEXT("execute"))
			|| !Link(Beta, TEXT("then"), BetaBody, TEXT("execute"))
			|| !Link(Producer, TEXT("ReturnValue"), AlphaBody, TEXT("InString"))
			|| !Link(Producer, TEXT("ReturnValue"), BetaBody, TEXT("InString"))) return false;
		if (bRetainProducer)
		{
			if (!Link(Retained, TEXT("then"), RetainedBody, TEXT("execute"))
				|| !Link(Producer, TEXT("ReturnValue"), RetainedBody, TEXT("InString"))) return false;
		}
		AlphaGuid = Alpha->NodeGuid;
		BetaGuid = Beta->NodeGuid;
		RetainedGuid = Retained->NodeGuid;
		ProducerGuid = Producer->NodeGuid;
		AlphaBodyGuid = AlphaBody->NodeGuid;
		BetaBodyGuid = BetaBody->NodeGuid;
		RetainedBodyGuid = RetainedBody->NodeGuid;
		if (bBlockAlpha)
		{
			UK2Node_CallFunction* RetainedData = AddCall(UKismetStringLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_IntToString")));
			if (!Link(Retained, TEXT("then"), RetainedBody, TEXT("execute"))
				|| !Link(Alpha, TEXT("Value"), RetainedData, TEXT("InInt"))
				|| !Link(RetainedData, TEXT("ReturnValue"), RetainedBody, TEXT("InString"))) return false;
		}
		return true;
	}

	/** Links two pins of this fixture's graph, reporting whether the schema accepted the connection. */
	bool LinkPins(UEdGraphNode* From, const TCHAR* FromName, UEdGraphNode* To, const TCHAR* ToName)
	{
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
		UEdGraphPin* FromPin = From ? From->FindPin(FName(FromName)) : nullptr;
		UEdGraphPin* ToPin = To ? To->FindPin(FName(ToName)) : nullptr;
		return Schema && FromPin && ToPin && Schema->TryCreateConnection(FromPin, ToPin);
	}

	/**
	 * Adds the late-retained producer chain the ownership cut must reach a fixed point over: the
	 * selected entry Alpha drives the execution body `RetainedProducer`, whose string output is
	 * consumed by the retained cosmetic body, and `RetainedProducer` drives the execution body
	 * `RetainedProducerConsumer`.
	 *
	 * The producer is retained only because a retained consumer uses its output. That decision is made
	 * after a candidate sweep over the island, so the consumer's incoming execution edge has to be
	 * re-checked afterwards; otherwise approving the consumer's removal severs the retained
	 * producer -> consumer execution link.
	 */
	bool AddLateRetainedProducerChain()
	{
		RetainedProducer = AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("GetConsoleVariableStringValue")));
		RetainedProducerConsumer = AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
		if (!RetainedProducer || !RetainedProducerConsumer) return false;
		if (!LinkPins(Retained, TEXT("then"), RetainedBody, TEXT("execute"))
			|| !LinkPins(Alpha, TEXT("then"), RetainedProducer, TEXT("execute"))
			|| !LinkPins(RetainedProducer, TEXT("then"), RetainedProducerConsumer, TEXT("execute"))
			|| !LinkPins(RetainedProducer, TEXT("ReturnValue"), RetainedBody, TEXT("InString"))) return false;
		RetainedProducerGuid = RetainedProducer->NodeGuid;
		RetainedProducerConsumerGuid = RetainedProducerConsumer->NodeGuid;
		return true;
	}

	/**
	 * Adds a data cycle between two pure producers that only removable candidates consume, and feeds
	 * the island's shared int producer from the cycle. The cycle therefore sits inside the scanned
	 * island: the partition must terminate on it and keep both members removable instead of refusing,
	 * spinning or reporting the cycle as unproven ownership.
	 */
	bool AddRemovableDataCycle()
	{
		CycleFirst = AddCall(UKismetStringLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_StringToInt")));
		CycleSecond = AddCall(UKismetStringLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_IntToString")));
		if (!CycleFirst || !CycleSecond) return false;
		if (!LinkPins(CycleSecond, TEXT("ReturnValue"), CycleFirst, TEXT("InString"))
			|| !LinkPins(CycleFirst, TEXT("ReturnValue"), CycleSecond, TEXT("InInt"))
			|| !LinkPins(CycleFirst, TEXT("ReturnValue"), Producer, TEXT("InInt"))) return false;
		CycleFirstGuid = CycleFirst->NodeGuid;
		CycleSecondGuid = CycleSecond->NodeGuid;
		return true;
	}


	bool RefreshPointers()
	{
		auto Find = [this](const FGuid& Guid) -> UEdGraphNode*
		{
			for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->NodeGuid == Guid) return Node;
			return nullptr;
		};
		Alpha = Cast<UK2Node_Event>(Find(AlphaGuid));
		Beta = Cast<UK2Node_Event>(Find(BetaGuid));
		Retained = Cast<UK2Node_Event>(Find(RetainedGuid));
		Producer = Cast<UK2Node_CallFunction>(Find(ProducerGuid));
		AlphaBody = Cast<UK2Node_CallFunction>(Find(AlphaBodyGuid));
		BetaBody = Cast<UK2Node_CallFunction>(Find(BetaBodyGuid));
		RetainedBody = Cast<UK2Node_CallFunction>(Find(RetainedBodyGuid));
		return Alpha && Beta && Retained && Producer && AlphaBody && BetaBody && RetainedBody;
	}
	TSharedPtr<FJsonObject> Migration(const TArray<FString>& Entries, const bool bApproved = false,
		const TArray<FString>& Approved = {}) const
	{
		TSharedPtr<FJsonObject> MigrationJson = MakeShared<FJsonObject>();
		MigrationJson->SetStringField(TEXT("op"), TEXT("retire_entries"));
		TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
		GraphRef->SetStringField(TEXT("graph_guid"), Graph->GraphGuid.ToString());
		Source->SetObjectField(TEXT("graph_ref"), GraphRef);
		TArray<TSharedPtr<FJsonValue>> EntryValues;
		for (const FString& Entry : Entries) EntryValues.Add(MakeShared<FJsonValueString>(Entry));
		Source->SetArrayField(TEXT("entry_node_guids"), EntryValues);
		MigrationJson->SetObjectField(TEXT("source"), Source);
		if (bApproved)
		{
			TArray<TSharedPtr<FJsonValue>> ApprovedValues;
			for (const FString& Entry : Approved) ApprovedValues.Add(MakeShared<FJsonValueString>(Entry));
			MigrationJson->SetArrayField(TEXT("approved_node_guids"), ApprovedValues);
		}
		return MigrationJson;
	}

	FString Filename() const
	{
		return Package ? FPackageName::LongPackageNameToFilename(Package->GetName(),
			FPackageName::GetAssetPackageExtension()) : FString();
	}

	bool SaveToDisk()
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Blueprint, *Filename(), SaveArgs);
	}

	void Cleanup()
	{
		if (Blueprint) { Blueprint->ClearFlags(RF_Standalone); Blueprint->MarkAsGarbage(); Blueprint = nullptr; }
		if (Package) { Package->ClearFlags(RF_Standalone); Package->MarkAsGarbage(); Package = nullptr; }
	}
};

struct FOperations
{
	int32 TargetCompiles = 0;
	int32 RecoveryCompiles = 0;
	int32 Saves = 0;
	/**
	 * Every completed editor Blueprint compilation while the watch is installed. `GEditor`
	 * broadcasts it independently of the patch seam, so a compile-on-load of a foreign Blueprint
	 * cannot hide behind the operation's own counters.
	 */
	int32 EditorCompiles = 0;

	void Begin()
	{
		Active = this;
		TargetCompiles = 0;
		RecoveryCompiles = 0;
		Saves = 0;
		EditorCompiles = 0;
		FCortexGraphPatchOps::SetOperationObserverForTesting([](const FName Operation, UBlueprint*)
		{
			if (!Active) return;
			if (Operation == TEXT("target_compile")) ++Active->TargetCompiles;
			else if (Operation == TEXT("recovery_compile")) ++Active->RecoveryCompiles;
		});
		SaveHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
			[](const FString&, UPackage*, FObjectPostSaveContext)
			{
				if (Active) ++Active->Saves;
			});
		if (GEditor)
		{
			CompileHandle = GEditor->OnBlueprintCompiled().AddLambda([this]() { ++EditorCompiles; });
		}
	}

	void End()
	{
		if (GEditor && CompileHandle.IsValid()) GEditor->OnBlueprintCompiled().Remove(CompileHandle);
		CompileHandle.Reset();
		UPackage::PackageSavedWithContextEvent.Remove(SaveHandle);
		FCortexGraphPatchOps::ClearOperationObserverForTesting();
		Active = nullptr;
	}

private:
	static FOperations* Active;
	FDelegateHandle SaveHandle;
	FDelegateHandle CompileHandle;
};

FOperations* FOperations::Active = nullptr;

TArray<uint8> ReadBytes(const FString& Filename)
{
	TArray<uint8> Bytes;
	FFileHelper::LoadFileToArray(Bytes, *Filename);
	return Bytes;
}

bool SameBytes(const TArray<uint8>& Left, const TArray<uint8>& Right)
{
	return Left.Num() > 0 && Left.Num() == Right.Num()
		&& FMemory::Memcmp(Left.GetData(), Right.GetData(), Left.Num()) == 0;
}

bool Plan(FFixture& Fixture, const TArray<FString>& Entries, FCortexGraphMigrationRetirePlan& OutPlan,
	bool& bReused, FCortexCommandResult& Error, const bool bApproved = false, const TArray<FString>& Approved = {})
{
	return FCortexGraphMigrationOps::PlanRetirement(Fixture.Blueprint,
		Fixture.Migration(Entries, bApproved, Approved), OutPlan, bReused, Error);
}
FString CaptureNativeGraph(UEdGraph* Graph, const TSet<FGuid>* Filter = nullptr)
{
	TArray<FString> Records;
	if (!Graph) return FString();
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		const FString Guid = Node->NodeGuid.ToString();
		if (Filter && !Filter->Contains(Node->NodeGuid)) continue;
		Records.Add(FString::Printf(TEXT("N|%s|%s|%d|%d|%s"), *Guid, *Node->GetClass()->GetPathName(),
			Node->NodePosX, Node->NodePosY, *Node->NodeComment));
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			Records.Add(FString::Printf(TEXT("P|%s|%s|%d|%s|%s|%s"), *Guid, *Pin->PinName.ToString(),
				static_cast<int32>(Pin->Direction), *Pin->PinType.PinCategory.ToString(), *Pin->DefaultValue,
				Pin->DefaultObject ? *Pin->DefaultObject->GetPathName() : TEXT("")));
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* FarNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (!FarNode || !LinkedPin || (Filter && !Filter->Contains(FarNode->NodeGuid))) continue;
				const FString From = Guid + TEXT(".") + Pin->PinName.ToString();
				const FString To = FarNode->NodeGuid.ToString() + TEXT(".") + LinkedPin->PinName.ToString();
				if (From < To) Records.Add(TEXT("L|") + From + TEXT("|") + To);
			}
		}
	}
	Records.Sort();
	return FString::Join(Records, TEXT("\n"));
}

/**
 * Two-stage reviewed retirement request for an arbitrary entry set: preview, echo the published
 * removable set, then approve it. `OutApproved` is the approved set the reviewed preview published.
 */
bool PrepareRetirementRequest(
	FFixture& Fixture,
	const TArray<FString>& Entries,
	const TCHAR* PatchId,
	TSharedPtr<FJsonObject>& OutRequest,
	TArray<FString>& OutApproved,
	FCortexCommandResult& OutError,
	const bool bCompile = false,
	const bool bSave = false)
{
	OutRequest = MakeShared<FJsonObject>();
	OutRequest->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	OutRequest->SetStringField(TEXT("patch_id"), PatchId);
	OutRequest->SetObjectField(TEXT("expected_fingerprint"), FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint));
	OutRequest->SetArrayField(TEXT("nodes"), {});
	OutRequest->SetArrayField(TEXT("connections"), {});
	OutRequest->SetArrayField(TEXT("pin_updates"), {});
	OutRequest->SetBoolField(TEXT("dry_run"), true);
	OutRequest->SetBoolField(TEXT("compile"), bCompile);
	OutRequest->SetBoolField(TEXT("save"), false);
	OutRequest->SetBoolField(TEXT("allow_noop"), false);
	TSharedPtr<FJsonObject> Migration = Fixture.Migration(Entries);
	OutRequest->SetObjectField(TEXT("migration"), Migration);

	FCortexGraphPreparedPatch Preview;
	if (!FCortexGraphPatchOps::Preflight(Fixture.Blueprint, OutRequest, Preview, OutError)) return false;
	const TSharedPtr<FJsonObject> Inventory =
		FCortexGraphMigrationOps::MakeRetirementInventory(Preview.RetirementPlan);
	const TArray<TSharedPtr<FJsonValue>>* Removable = nullptr;
	if (!Inventory.IsValid() || !Inventory->TryGetArrayField(TEXT("removable"), Removable) || !Removable)
	{
		OutError = FCortexCommandRouter::Error(CortexErrorCodes::InvalidOperation,
			TEXT("retirement preview did not publish its removable set"));
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Removable) OutApproved.Add(Value->AsString());
	TArray<TSharedPtr<FJsonValue>> ApprovalValues;
	for (const FString& Guid : OutApproved) ApprovalValues.Add(MakeShared<FJsonValueString>(Guid));
	Migration->SetArrayField(TEXT("approved_node_guids"), ApprovalValues);

	FCortexGraphPreparedPatch Reviewed;
	if (!FCortexGraphPatchOps::Preflight(Fixture.Blueprint, OutRequest, Reviewed, OutError)) return false;
	OutRequest->SetBoolField(TEXT("dry_run"), false);
	OutRequest->SetBoolField(TEXT("save"), bSave);
	OutRequest->SetStringField(TEXT("expected_validation_hash"), Reviewed.ValidationHash);
	return true;
}

/** The reviewed retirement request of the fixture's two legacy entries. */
bool PrepareApprovedRequest(
	FFixture& Fixture,
	const TCHAR* PatchId,
	TSharedPtr<FJsonObject>& OutRequest,
	TArray<FString>& OutApproved,
	FCortexCommandResult& OutError,
	const bool bCompile = false,
	const bool bSave = false)
{
	return PrepareRetirementRequest(Fixture,
		{ Fixture.Alpha->NodeGuid.ToString(), Fixture.Beta->NodeGuid.ToString() },
		PatchId, OutRequest, OutApproved, OutError, bCompile, bSave);
}

/** Canonical widget-tree signature, so preview purity covers the preserved designer tree. */
FString CaptureWidgetTree(const UWidgetBlueprint* Blueprint)
{
	TArray<FString> Records;
	if (!Blueprint) return FString();
	for (const UWidget* Widget : Blueprint->GetAllSourceWidgets())
	{
		if (!Widget) continue;
		Records.Add(FString::Printf(TEXT("%s|%s|%d"), *Widget->GetName(),
			*Widget->GetClass()->GetPathName(), Widget->bIsVariable ? 1 : 0));
	}
	if (Blueprint->WidgetTree && Blueprint->WidgetTree->RootWidget)
	{
		Records.Add(TEXT("root|") + Blueprint->WidgetTree->RootWidget->GetName());
	}
	Records.Sort();
	return FString::Join(Records, TEXT("\n"));
}

/** Canonical generated component-delegate binding signature (component|delegate|function). */
FString CaptureComponentBindings(const UBlueprint* Blueprint)
{
	TArray<FString> Records;
	const UBlueprintGeneratedClass* GeneratedClass = Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass) : nullptr;
	if (!GeneratedClass) return FString();
	for (const UDynamicBlueprintBinding* Binding : GeneratedClass->DynamicBindingObjects)
	{
		const UComponentDelegateBinding* ComponentBinding = Cast<UComponentDelegateBinding>(Binding);
		if (!ComponentBinding) continue;
		for (const FBlueprintComponentDelegateBinding& Entry : ComponentBinding->ComponentDelegateBindings)
		{
			Records.Add(FString::Printf(TEXT("%s|%s|%s"), *Entry.ComponentPropertyName.ToString(),
				*Entry.DelegatePropertyName.ToString(), *Entry.FunctionNameToBind.ToString()));
		}
	}
	Records.Sort();
	return FString::Join(Records, TEXT("\n"));
}

/**
 * Guarded-retirement fixture for the entry classes CortexSandbox issue #112 must accept.
 *
 * Everything is built on a real Widget Blueprint through engine APIs: the widget variable is created
 * by the widget compiler, the component-bound node binds to that compiled component property plus the
 * native `UButton::OnClicked` delegate exactly as `FKismetEditorUtilities::CreateNewBoundEventForComponent`
 * and `UBlueprintBoundEventNodeSpawner::BindToNode` do, and the custom event is a compiled callable
 * generated function. One unrelated cosmetic override stays in the graph so preservation is asserted
 * next to eligibility. The Blueprint parent is a native widget class because a plain `UUserWidget`
 * parent is not proof that native behaviour replaced a retired widget lifecycle body.
 */
struct FGuardedEntryFixture
{
	UPackage* Package = nullptr;
	UWidgetBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UK2Node_ComponentBoundEvent* Bound = nullptr;
	UK2Node_Event* Construct = nullptr;
	UK2Node_Event* Initialized = nullptr;
	UK2Node_Event* Retained = nullptr;
	UK2Node_CustomEvent* Custom = nullptr;
	UK2Node_CallFunction* BoundBody = nullptr;
	UK2Node_CallFunction* ConstructBody = nullptr;
	UK2Node_CallFunction* InitializedBody = nullptr;
	UK2Node_CallFunction* RetainedBody = nullptr;
	UK2Node_CallFunction* CustomBody = nullptr;
	/** The compiled widget variable and native delegate the component-bound node binds. */
	FObjectProperty* ComponentProperty = nullptr;
	FMulticastDelegateProperty* ClickedDelegate = nullptr;
	/** A compiled widget variable whose own class cannot host the native delegate above. */
	FObjectProperty* IncompatibleComponentProperty = nullptr;
	FGuid BoundGuid;
	FGuid ConstructGuid;
	FGuid InitializedGuid;
	FGuid RetainedGuid;
	FGuid CustomGuid;
	FGuid BoundBodyGuid;
	FGuid ConstructBodyGuid;
	FGuid InitializedBodyGuid;
	FGuid RetainedBodyGuid;
	FGuid CustomBodyGuid;

	TSharedPtr<FJsonObject> Migration(const TArray<FString>& Entries, const bool bApproved = false,
		const TArray<FString>& Approved = {}) const
	{
		TSharedPtr<FJsonObject> MigrationJson = MakeShared<FJsonObject>();
		MigrationJson->SetStringField(TEXT("op"), TEXT("retire_entries"));
		TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
		GraphRef->SetStringField(TEXT("graph_guid"), Graph->GraphGuid.ToString());
		Source->SetObjectField(TEXT("graph_ref"), GraphRef);
		TArray<TSharedPtr<FJsonValue>> EntryValues;
		for (const FString& Entry : Entries) EntryValues.Add(MakeShared<FJsonValueString>(Entry));
		Source->SetArrayField(TEXT("entry_node_guids"), EntryValues);
		MigrationJson->SetObjectField(TEXT("source"), Source);
		if (bApproved)
		{
			TArray<TSharedPtr<FJsonValue>> ApprovedValues;
			for (const FString& Entry : Approved) ApprovedValues.Add(MakeShared<FJsonValueString>(Entry));
			MigrationJson->SetArrayField(TEXT("approved_node_guids"), ApprovedValues);
		}
		return MigrationJson;
	}

	UK2Node_CallFunction* AddCall(UFunction* Function)
	{
		if (!Graph || !Function) return nullptr;
		UK2Node_CallFunction* Call = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		Call->FunctionReference.SetExternalMember(Function->GetFName(), Function->GetOuterUClass());
		Call->CreateNewGuid();
		Call->AllocateDefaultPins();
		Graph->AddNode(Call, true, false);
		return Call;
	}

	UK2Node_Event* AddOverride(const TCHAR* Member, UClass* MemberClass)
	{
		if (!Graph || !MemberClass) return nullptr;
		UK2Node_Event* Event = NewObject<UK2Node_Event>(Graph, NAME_None, RF_Transactional);
		Event->EventReference.SetExternalMember(FName(Member), MemberClass);
		Event->bOverrideFunction = true;
		Event->CreateNewGuid();
		Event->AllocateDefaultPins();
		Graph->AddNode(Event, true, false);
		return Event;
	}

	bool LinkNodes(UEdGraphNode* From, const TCHAR* FromPin, UEdGraphNode* To, const TCHAR* ToPin)
	{
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
		UEdGraphPin* Source = From ? From->FindPin(FName(FromPin)) : nullptr;
		UEdGraphPin* Target = To ? To->FindPin(FName(ToPin)) : nullptr;
		return Schema && Source && Target && Schema->TryCreateConnection(Source, Target);
	}

	/** Every selected entry owns a real downstream body, so the partition runs on a genuine island. */
	UK2Node_CallFunction* AddBody(UEdGraphNode* Entry)
	{
		UK2Node_CallFunction* Body = AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
		if (!Body || !LinkNodes(Entry, TEXT("then"), Body, TEXT("execute"))) return nullptr;
		return Body;
	}

	UEdGraphNode* FindByGuid(const FGuid& Guid) const
	{
		if (!Graph) return nullptr;
		for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->NodeGuid == Guid) return Node;
		return nullptr;
	}

	/** True only when every fixture node and graph the guarded tests dereference exists. */
	bool IsComplete() const
	{
		return Blueprint && Graph && Bound && Construct && Initialized && Retained && Custom
			&& BoundBody && ConstructBody && InitializedBody && RetainedBody && CustomBody;
	}

	/** On-disk path of the fixture asset, so preview purity also covers the persisted bytes. */
	FString Filename() const
	{
		return Package ? FPackageName::LongPackageNameToFilename(Package->GetName(),
			FPackageName::GetAssetPackageExtension()) : FString();
	}

	bool SaveToDisk()
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Blueprint, *Filename(), SaveArgs);
	}

	bool RefreshPointers()
	{
		Bound = Cast<UK2Node_ComponentBoundEvent>(FindByGuid(BoundGuid));
		Construct = Cast<UK2Node_Event>(FindByGuid(ConstructGuid));
		Initialized = Cast<UK2Node_Event>(FindByGuid(InitializedGuid));
		Retained = Cast<UK2Node_Event>(FindByGuid(RetainedGuid));
		Custom = Cast<UK2Node_CustomEvent>(FindByGuid(CustomGuid));
		BoundBody = Cast<UK2Node_CallFunction>(FindByGuid(BoundBodyGuid));
		ConstructBody = Cast<UK2Node_CallFunction>(FindByGuid(ConstructBodyGuid));
		InitializedBody = Cast<UK2Node_CallFunction>(FindByGuid(InitializedBodyGuid));
		RetainedBody = Cast<UK2Node_CallFunction>(FindByGuid(RetainedBodyGuid));
		CustomBody = Cast<UK2Node_CallFunction>(FindByGuid(CustomBodyGuid));
		return Bound && Construct && Initialized && Retained && Custom
			&& BoundBody && ConstructBody && InitializedBody && RetainedBody && CustomBody;
	}

	/** `bAddIncompatibleComponent` compiles a second widget variable of an unrelated widget class. */
	bool Build(const TCHAR* Name, const bool bAddIncompatibleComponent = false)
	{
		EnsureCortexGraphTestTempContentRoot();
		Package = CreatePackage(*FString::Printf(TEXT("/Game/Temp/%s"), Name));
		Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UCortexGraphRetireNativeLifecycleWidget::StaticClass(), Package, FName(Name), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		if (!Blueprint) return false;

		UWidgetTree* Tree = Blueprint->WidgetTree;
		if (!Tree)
		{
			Tree = NewObject<UWidgetTree>(Blueprint, UWidgetTree::StaticClass(), TEXT("WidgetTree"));
			Blueprint->WidgetTree = Tree;
		}
		UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		UButton* ActionButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ActionButton"));
		if (!Root || !ActionButton) return false;
		Tree->RootWidget = Root;
		ActionButton->bIsVariable = true;
		Root->AddChild(ActionButton);
		if (bAddIncompatibleComponent)
		{
			UCanvasPanel* IncompatiblePanel =
				Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("IncompatiblePanel"));
			if (!IncompatiblePanel) return false;
			IncompatiblePanel->bIsVariable = true;
			Root->AddChild(IncompatiblePanel);
		}

		if (Blueprint->UbergraphPages.Num() == 0)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, UEdGraphSchema_K2::GN_EventGraph,
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
		}
		else Graph = Blueprint->UbergraphPages[0];
		if (!Graph) return false;

		// The widget variable becomes a generated object property only after a widget compile, and the
		// component-bound node must bind to that real property.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		FObjectProperty* FoundComponentProperty = Blueprint->SkeletonGeneratedClass
			? FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, TEXT("ActionButton")) : nullptr;
		if (!FoundComponentProperty && Blueprint->GeneratedClass)
		{
			FoundComponentProperty = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, TEXT("ActionButton"));
		}
		if (bAddIncompatibleComponent)
		{
			IncompatibleComponentProperty = Blueprint->SkeletonGeneratedClass
				? FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, TEXT("IncompatiblePanel")) : nullptr;
			if (!IncompatibleComponentProperty && Blueprint->GeneratedClass)
			{
				IncompatibleComponentProperty =
					FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, TEXT("IncompatiblePanel"));
			}
			if (!IncompatibleComponentProperty) return false;
		}
		FMulticastDelegateProperty* FoundClickedDelegate =
			FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), TEXT("OnClicked"));
		if (!FoundComponentProperty || !FoundClickedDelegate) return false;
		ComponentProperty = FoundComponentProperty;
		ClickedDelegate = FoundClickedDelegate;

		Retained = AddOverride(TEXT("OnRetainedCosmeticEvent"), UCortexGraphRetireNativeLifecycleWidget::StaticClass());
		RetainedBody = AddBody(Retained);
		Construct = AddOverride(TEXT("Construct"), UCortexGraphRetireNativeLifecycleWidget::StaticClass());
		ConstructBody = AddBody(Construct);
		Initialized = AddOverride(TEXT("OnInitialized"), UCortexGraphRetireNativeLifecycleWidget::StaticClass());
		InitializedBody = AddBody(Initialized);

		Custom = NewObject<UK2Node_CustomEvent>(Graph, NAME_None, RF_Transactional);
		Custom->CustomFunctionName = TEXT("OnLegacyTutorialEvent");
		Custom->CreateNewGuid();
		Custom->AllocateDefaultPins();
		Graph->AddNode(Custom, true, false);
		CustomBody = AddBody(Custom);

		// Same sequence the editor uses for a component binding: initialize from the compiled component
		// property and the native delegate, add the node, then allocate the event pins.
		Bound = NewObject<UK2Node_ComponentBoundEvent>(Graph, NAME_None, RF_Transactional);
		Bound->CreateNewGuid();
		Bound->InitializeComponentBoundEventParams(ComponentProperty, ClickedDelegate);
		Graph->AddNode(Bound, true, false);
		Bound->AllocateDefaultPins();
		BoundBody = AddBody(Bound);

		if (!Retained || !RetainedBody || !Construct || !ConstructBody || !Initialized || !InitializedBody
			|| !Custom || !CustomBody || !Bound || !BoundBody) return false;
		RetainedGuid = Retained->NodeGuid;
		RetainedBodyGuid = RetainedBody->NodeGuid;
		ConstructGuid = Construct->NodeGuid;
		ConstructBodyGuid = ConstructBody->NodeGuid;
		InitializedGuid = Initialized->NodeGuid;
		InitializedBodyGuid = InitializedBody->NodeGuid;
		CustomGuid = Custom->NodeGuid;
		CustomBodyGuid = CustomBody->NodeGuid;
		BoundGuid = Bound->NodeGuid;
		BoundBodyGuid = BoundBody->NodeGuid;

		// The second compile materializes the generated binding function and the callable custom event
		// function, and registers the real dynamic component delegate binding on the generated class.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Bound) Bound->ReconstructNode();
		return RefreshPointers();
	}

	void Cleanup()
	{
		if (Blueprint) { Blueprint->ClearFlags(RF_Standalone); Blueprint->MarkAsGarbage(); Blueprint = nullptr; }
		if (Package) { Package->ClearFlags(RF_Standalone); Package->MarkAsGarbage(); Package = nullptr; }
	}
};

bool Plan(FGuardedEntryFixture& Fixture, const TArray<FString>& Entries, FCortexGraphMigrationRetirePlan& OutPlan,
	bool& bReused, FCortexCommandResult& Error, const bool bApproved = false, const TArray<FString>& Approved = {})
{
	return FCortexGraphMigrationOps::PlanRetirement(Fixture.Blueprint,
		Fixture.Migration(Entries, bApproved, Approved), OutPlan, bReused, Error);
}

/** Two-stage guarded apply request: preview, echo the published removable set, then approve it. */
bool PrepareGuardedRequest(
	FGuardedEntryFixture& Fixture,
	const TArray<FString>& Selected,
	const TCHAR* PatchId,
	TSharedPtr<FJsonObject>& OutRequest,
	TArray<FString>& OutApproved,
	FCortexCommandResult& OutError,
	const bool bCompile = true,
	const bool bSave = false)
{
	OutRequest = MakeShared<FJsonObject>();
	OutRequest->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	OutRequest->SetStringField(TEXT("patch_id"), PatchId);
	OutRequest->SetObjectField(TEXT("expected_fingerprint"), FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint));
	OutRequest->SetArrayField(TEXT("nodes"), {});
	OutRequest->SetArrayField(TEXT("connections"), {});
	OutRequest->SetArrayField(TEXT("pin_updates"), {});
	OutRequest->SetBoolField(TEXT("dry_run"), true);
	OutRequest->SetBoolField(TEXT("compile"), bCompile);
	OutRequest->SetBoolField(TEXT("save"), false);
	OutRequest->SetBoolField(TEXT("allow_noop"), false);
	TSharedPtr<FJsonObject> Migration = Fixture.Migration(Selected);
	OutRequest->SetObjectField(TEXT("migration"), Migration);

	FCortexGraphPreparedPatch Preview;
	if (!FCortexGraphPatchOps::Preflight(Fixture.Blueprint, OutRequest, Preview, OutError)) return false;
	const TSharedPtr<FJsonObject> Inventory =
		FCortexGraphMigrationOps::MakeRetirementInventory(Preview.RetirementPlan);
	const TArray<TSharedPtr<FJsonValue>>* Removable = nullptr;
	if (!Inventory.IsValid() || !Inventory->TryGetArrayField(TEXT("removable"), Removable) || !Removable)
	{
		OutError = FCortexCommandRouter::Error(CortexErrorCodes::InvalidOperation,
			TEXT("guarded retirement preview did not publish its removable set"));
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Removable) OutApproved.Add(Value->AsString());
	TArray<TSharedPtr<FJsonValue>> ApprovalValues;
	for (const FString& Guid : OutApproved) ApprovalValues.Add(MakeShared<FJsonValueString>(Guid));
	Migration->SetArrayField(TEXT("approved_node_guids"), ApprovalValues);

	FCortexGraphPreparedPatch Reviewed;
	if (!FCortexGraphPatchOps::Preflight(Fixture.Blueprint, OutRequest, Reviewed, OutError)) return false;
	OutRequest->SetBoolField(TEXT("dry_run"), false);
	OutRequest->SetBoolField(TEXT("save"), bSave);
	OutRequest->SetStringField(TEXT("expected_validation_hash"), Reviewed.ValidationHash);
	return true;
}

/**
 * Preview-only guarded request: the refusal must arrive before any approval echo can exist, so the
 * published removable set is deliberately not echoed back here.
 */
bool PreviewRetirement(
	FGuardedEntryFixture& Fixture,
	const TArray<FString>& Selected,
	const TCHAR* PatchId,
	FCortexGraphPreparedPatch& OutPreview,
	FCortexCommandResult& OutError)
{
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	Request->SetStringField(TEXT("patch_id"), PatchId);
	Request->SetObjectField(TEXT("expected_fingerprint"), FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint));
	Request->SetArrayField(TEXT("nodes"), {});
	Request->SetArrayField(TEXT("connections"), {});
	Request->SetArrayField(TEXT("pin_updates"), {});
	Request->SetBoolField(TEXT("dry_run"), true);
	Request->SetBoolField(TEXT("compile"), true);
	Request->SetBoolField(TEXT("save"), false);
	Request->SetBoolField(TEXT("allow_noop"), false);
	Request->SetObjectField(TEXT("migration"), Fixture.Migration(Selected));
	return FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, OutPreview, OutError);
}

/**
 * A real second Widget Blueprint asset whose event graph calls one generated function of another
 * asset. `UK2Node_CustomEvent` compiles its node into a `BlueprintCallable` `Public` function, so
 * this is exactly the cross-asset call site a retirement must not silently sever.
 */
struct FExternalCallerFixture
{
	UPackage* Package = nullptr;
	UWidgetBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UK2Node_CallFunction* Caller = nullptr;
	/** The asset name the package holds, kept after the asset object itself is unloaded. */
	FName AssetName;

	/** The class of the target asset that owns its compiled custom-event generated function. */
	static UClass* ResolveCallableOwner(UBlueprint* Target, const FName FunctionName)
	{
		UClass* const Generated = Target ? Target->GeneratedClass : nullptr;
		return Generated && Generated->FindFunctionByName(FunctionName) ? Generated : nullptr;
	}

	FString Filename() const
	{
		return Package ? FPackageName::LongPackageNameToFilename(Package->GetName(),
			FPackageName::GetAssetPackageExtension()) : FString();
	}

	FString PackageName() const
	{
		return Package ? Package->GetName() : FString();
	}

	FString ObjectPath() const
	{
		return !Package || AssetName.IsNone()
			? FString() : Package->GetName() + TEXT(".") + AssetName.ToString();
	}

	bool SaveToDisk()
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return UPackage::SavePackage(Package, Blueprint, *Filename(), SaveArgs);
	}

	/**
	 * Drops the asset object while keeping its package resident: the exact state a referencer is in
	 * when the editor still holds the `UPackage` (a dirty tracker, a pending reload, a partially
	 * loaded package) but the Blueprint inside it was garbage collected. `FindPackage` alone cannot
	 * tell that state apart from an inspectable referencer, which is the guard under test.
	 *
	 * Uses the engine's own unload idiom (`UPackageTools::UnloadPackages`): root the package so it
	 * survives, clear `RF_Standalone` on the objects inside it, detach the linker, then collect.
	 */
	bool UnloadAssetKeepingPackageResident()
	{
		if (!Package || !Blueprint) return false;
		const FString ResidentPackageName = Package->GetName();
		if (GEditor && GEditor->Trans)
		{
			GEditor->Trans->Reset(FText::FromString(TEXT("CortexGraphMigrationRetireReferencerUnload")));
		}
		Package->SetFlags(RF_Standalone);
		Blueprint->ClearEditorReferences();
		Blueprint->ClearFlags(RF_Standalone);
		Blueprint->MarkAsGarbage();
		ResetLoaders(Package);
		Package->SetDirtyFlag(false);
		Blueprint = nullptr;
		Graph = nullptr;
		Caller = nullptr;
		FlushAsyncLoading();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		UPackage* const ResidentPackage = FindPackage(nullptr, *ResidentPackageName);
		Package = ResidentPackage;
		if (ResidentPackage) ResidentPackage->SetDirtyFlag(false);
		return ResidentPackage != nullptr && FindObject<UBlueprint>(ResidentPackage, *AssetName.ToString()) == nullptr;
	}

	/**
	 * Unloads the referencer entirely, leaving only its bytes on disk: a referencer the registry
	 * reports but nothing in memory can inspect without loading it.
	 */
	bool UnloadCompletely()
	{
		if (!Package || !Blueprint) return false;
		const FString UnloadedPackageName = Package->GetName();
		if (GEditor && GEditor->Trans)
		{
			GEditor->Trans->Reset(FText::FromString(TEXT("CortexGraphMigrationRetireReferencerUnload")));
		}
		Blueprint->ClearEditorReferences();
		Blueprint->ClearFlags(RF_Standalone);
		Blueprint->MarkAsGarbage();
		Package->ClearFlags(RF_Standalone);
		Package->MarkAsGarbage();
		ResetLoaders(Package);
		Blueprint = nullptr;
		Graph = nullptr;
		Caller = nullptr;
		Package = nullptr;
		FlushAsyncLoading();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		return FindPackage(nullptr, *UnloadedPackageName) == nullptr;
	}

	bool Build(const TCHAR* Name, UBlueprint* Target, const FName FunctionName)
	{
		UClass* const OwnerClass = ResolveCallableOwner(Target, FunctionName);
		if (!OwnerClass) return false;
		EnsureCortexGraphTestTempContentRoot();
		Package = CreatePackage(*FString::Printf(TEXT("/Game/Temp/%s"), Name));
		Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), Package, FName(Name), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		if (!Blueprint) return false;
		AssetName = FName(Name);

		UWidgetTree* Tree = Blueprint->WidgetTree;
		if (!Tree)
		{
			Tree = NewObject<UWidgetTree>(Blueprint, UWidgetTree::StaticClass(), TEXT("WidgetTree"));
			Blueprint->WidgetTree = Tree;
		}
		UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		if (!Root) return false;
		Tree->RootWidget = Root;

		if (Blueprint->UbergraphPages.Num() == 0)
		{
			Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, UEdGraphSchema_K2::GN_EventGraph,
				UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
			FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
		}
		else Graph = Blueprint->UbergraphPages[0];
		if (!Graph) return false;

		Caller = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		// `SetExternalMember` runs the parent through `GetAuthoritativeClass`, so a Blueprint parent is
		// stored as its generated class: the real owner class a cross-asset call node references.
		Caller->FunctionReference.SetExternalMember(FunctionName, OwnerClass);
		Caller->CreateNewGuid();
		Caller->AllocateDefaultPins();
		Graph->AddNode(Caller, true, false);
		return Caller->FindPin(TEXT("execute")) != nullptr;
	}

	void Cleanup()
	{
		if (Blueprint) { Blueprint->ClearFlags(RF_Standalone); Blueprint->MarkAsGarbage(); Blueprint = nullptr; }
		if (Package) { Package->ClearFlags(RF_Standalone); Package->MarkAsGarbage(); Package = nullptr; }
	}
};

/**
 * Removes every in-memory trace of one referencer package, whatever state the preview left it in:
 * the resident package without its asset, or the package a preview auto-loaded. Ends with a
 * collection so the file can be deleted afterwards.
 */
void PurgeReferencerPackage(const FString& PackageName)
{
	if (UPackage* const Resident = FindPackage(nullptr, *PackageName))
	{
		if (GEditor && GEditor->Trans)
		{
			GEditor->Trans->Reset(FText::FromString(TEXT("CortexGraphMigrationRetireReferencerCleanup")));
		}
		TArray<UObject*> ObjectsInPackage;
		GetObjectsWithPackage(Resident, ObjectsInPackage);
		for (UObject* Object : ObjectsInPackage)
		{
			if (UBlueprint* const LoadedBlueprint = Cast<UBlueprint>(Object)) LoadedBlueprint->ClearEditorReferences();
		}
		for (UObject* Object : ObjectsInPackage)
		{
			if (Object && Object->HasAnyFlags(RF_Standalone)) Object->ClearFlags(RF_Standalone);
		}
		Resident->ClearFlags(RF_Standalone);
		Resident->SetDirtyFlag(false);
		Resident->MarkAsGarbage();
		ResetLoaders(Resident);
	}
	FlushAsyncLoading();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
}

/**
 * Makes the editor Asset Registry aware of the two just-saved packages by reading their files, then
 * verifies the referencer relationship the preview is about to be asked about. The Asset Registry
 * is the only source of on-disk referencer identities, so the fixture proves its own precondition
 * and reports the limitation instead of silently testing an unrelated code path.
 */
bool ScanSavedReferencers(
	const TArray<FString>& Files,
	const FName TargetPackageName,
	const FName ReferencerPackageName,
	FString& OutFailure)
{
	OutFailure.Reset();
	IAssetRegistry* const AssetRegistry = IAssetRegistry::Get();
	if (!AssetRegistry)
	{
		OutFailure = TEXT("the Asset Registry is unavailable");
		return false;
	}
	AssetRegistry->WaitForCompletion();
	if (AssetRegistry->IsLoadingAssets())
	{
		OutFailure = TEXT("the Asset Registry is still loading assets, so the referencer investigation would be incomplete for an unrelated reason");
		return false;
	}
	AssetRegistry->ScanFilesSynchronous(Files, /*bForceRescan=*/true);
	if (AssetRegistry->IsLoadingAssets())
	{
		OutFailure = TEXT("the Asset Registry is still loading assets after the fixture scan, so the referencer investigation would be incomplete for an unrelated reason");
		return false;
	}
	TArray<FName> Referencers;
	if (!AssetRegistry->GetReferencers(TargetPackageName, Referencers,
		UE::AssetRegistry::EDependencyCategory::Package))
	{
		OutFailure = FString::Printf(TEXT("the Asset Registry has no dependency node for target package '%s'"),
			*TargetPackageName.ToString());
		return false;
	}
	if (!Referencers.Contains(ReferencerPackageName))
	{
		TArray<FString> ReportedNames;
		for (const FName& Referencer : Referencers) ReportedNames.Add(Referencer.ToString());
		OutFailure = FString::Printf(
			TEXT("the saved referencer package '%s' is not reported for target package '%s' (reported: %s)"),
			*ReferencerPackageName.ToString(), *TargetPackageName.ToString(),
			ReportedNames.IsEmpty() ? TEXT("none") : *FString::Join(ReportedNames, TEXT(", ")));
		return false;
	}
	return true;
}

bool ReferencerRefusalIsDiagnostic(const FCortexCommandResult& Error, const FString& ReferencerName)
{
	const bool bNamesReferencer = Error.ErrorMessage.Contains(ReferencerName)
		|| Error.ErrorMessage.Contains(TEXT("BP_RetireResidentReferencerHost"))
		|| Error.ErrorMessage.Contains(TEXT("BP_RetireNonresidentReferencerHost"));
	const bool bExplainsWhy = Error.ErrorMessage.Contains(TEXT("inspect"))
		|| Error.ErrorMessage.Contains(TEXT("not loaded"))
		|| Error.ErrorMessage.Contains(TEXT("could not be loaded"))
		|| Error.ErrorMessage.Contains(TEXT("cannot be ruled out"))
		|| Error.ErrorMessage.Contains(TEXT("unloaded"))
		|| Error.ErrorMessage.Contains(TEXT("resident"));
	return bNamesReferencer && bExplainsWhy;
}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireSelectedEntriesTest,
	"Cortex.Graph.Authoring.Migration.Retire.SelectedEntriesAndPrivateProducer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireSelectedEntriesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireSelected")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("retirement plan is computed: %s"), *Error.ErrorMessage),
		Plan(Fixture, { Fixture.Alpha->NodeGuid.ToString(), Fixture.Beta->NodeGuid.ToString() }, PlanValue, bReused, Error));
	TestTrue(TEXT("alpha is removable"), PlanValue.RemovableGuids.Contains(Fixture.Alpha->NodeGuid.ToString()));
	TestTrue(TEXT("beta is removable"), PlanValue.RemovableGuids.Contains(Fixture.Beta->NodeGuid.ToString()));
	TestTrue(TEXT("producer shared only by selected entries is removable"), PlanValue.RemovableGuids.Contains(Fixture.Producer->NodeGuid.ToString()));
	FCortexGraphMigrationRetirePlan RoundTrip;
	TestTrue(TEXT("durable plan round-trips"), FCortexGraphMigrationRetirePlan::FromJson(PlanValue.ToJson(), RoundTrip, Error));
	TestEqual(TEXT("round-trip keeps selected GUIDs"), RoundTrip.SelectedEntryGuids.Num(), 2);
	const TSharedPtr<FJsonObject> Inventory = FCortexGraphMigrationOps::MakeRetirementInventory(PlanValue.ToJson());
	TestNotNull(TEXT("retirement inventory is created"), Inventory.Get());
	if (Inventory.IsValid())
	{
		FString Status;
		TestTrue(TEXT("inventory reports explicit Blueprint status"), Inventory->TryGetStringField(TEXT("blueprint_status_before"), Status)
			&& Status.StartsWith(TEXT("BS_")));
		TestEqual(TEXT("inventory names cached diagnostic source"),
			Inventory->GetStringField(TEXT("preexisting_diagnostics_source")), FString(TEXT("cached_node_messages")));
	}
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireStaleLegacyOverrideTest,
	"Cortex.Graph.Authoring.Migration.Retire.StaleLegacyOverrideAfterReparent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireStaleLegacyOverrideTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("reparented Widget fixture is created"), Fixture.Build(TEXT("BP_RetireStaleLegacy"),
		false, false, UCortexGraphRetireTargetWidget::StaticClass()));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the Widget BP is reparented away from the stale event parent"),
		!Fixture.Blueprint->ParentClass->IsChildOf(UCortexGraphRetireLegacyWidget::StaticClass()));
	TestEqual(TEXT("the event retains its legacy parent identity"),
		Fixture.Alpha->EventReference.GetMemberParentClass(), UCortexGraphRetireLegacyWidget::StaticClass());
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const FString AlphaGuid = Fixture.Alpha->NodeGuid.ToString();
	TestTrue(FString::Printf(TEXT("stale legacy override remains eligible for retirement: %s"), *Error.ErrorMessage),
		Plan(Fixture, { AlphaGuid }, PlanValue, bReused, Error));
	TestTrue(TEXT("stale legacy override is removable"),
		PlanValue.RemovableGuids.Contains(AlphaGuid));
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireSharedProducerTest,
	"Cortex.Graph.Authoring.Migration.Retire.RetainsSharedProducer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireSharedProducerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireShared"), true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("retirement plan is computed: %s"), *Error.ErrorMessage),
		Plan(Fixture, { Fixture.Alpha->NodeGuid.ToString(), Fixture.Beta->NodeGuid.ToString() }, PlanValue, bReused, Error));
	TestTrue(TEXT("producer is reported shared"), PlanValue.Shared.ContainsByPredicate([&](const FCortexGraphPruneNode& Node)
		{ return Node.NodeGuid == Fixture.Producer->NodeGuid.ToString(); }));
	TestFalse(TEXT("shared producer is not removable"), PlanValue.RemovableGuids.Contains(Fixture.Producer->NodeGuid.ToString()));
	Fixture.Cleanup();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireSharedExecutionBodyTest,
	"Cortex.Graph.Authoring.Migration.Retire.RefusesSharedExecutionBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireSharedExecutionBodyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireSharedExecutionBody")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const UEdGraphSchema* Schema = Fixture.Graph->GetSchema();
	UEdGraphPin* RetainedThen = Fixture.Retained->FindPin(TEXT("then"));
	UEdGraphPin* SharedBodyExecute = Fixture.AlphaBody->FindPin(TEXT("execute"));
	TestTrue(TEXT("retained entry can share the selected entry's execution body"),
		Schema && RetainedThen && SharedBodyExecute && Schema->TryCreateConnection(RetainedThen, SharedBodyExecute));
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const FString SelectedGuid = Fixture.Alpha->NodeGuid.ToString();
	TestTrue(FString::Printf(TEXT("retirement preview succeeds: %s"), *Error.ErrorMessage),
		Plan(Fixture, { SelectedGuid }, PlanValue, bReused, Error));
	TestTrue(TEXT("execution body with an incoming retained execution edge is blocked"),
		PlanValue.Blocked.ContainsByPredicate([&](const FCortexGraphPruneNode& Node)
			{ return Node.NodeGuid == Fixture.AlphaBody->NodeGuid.ToString(); }));
	TestFalse(TEXT("blocked execution body is not approved for removal"),
		PlanValue.RemovableGuids.Contains(Fixture.AlphaBody->NodeGuid.ToString()));
	TestTrue(TEXT("selected entry feeding the retained body is blocked"),
		PlanValue.Blocked.ContainsByPredicate([&](const FCortexGraphPruneNode& Node)
			{ return Node.NodeGuid == SelectedGuid; }));
	TestFalse(TEXT("selected entry is not approved for removal"), PlanValue.RemovableGuids.Contains(SelectedGuid));

	Error = FCortexCommandResult();
	TestFalse(TEXT("reviewed retirement refuses the blocked shared execution body"),
		Plan(Fixture, { SelectedGuid }, PlanValue, bReused, Error, true, PlanValue.RemovableGuids));
	TestEqual(TEXT("shared execution body refusal is INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
	TestEqual(TEXT("refused retirement leaves the complete graph unchanged"),
		CaptureNativeGraph(Fixture.Graph), GraphBefore);

	Fixture.Cleanup();
	return true;
}

// RIP-44 / CortexSandbox #112 partition regression. The selected obsolete entry `Alpha` drives the
// execution body A, A's string output is consumed by the retained cosmetic body, and A drives the
// execution body B. A is retained only because that retained consumer uses its output, and that
// decision lands after a one-pass candidate sweep had already accepted B, so approving B deleted the
// retained A -> B execution link. The cut must reach a fixed point across both link kinds: B stays
// retained, or the reviewed request refuses instead of severing the retained link. A different,
// independent selected entry must still be retired, so the new closure may not over-retain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireLateRetainedProducerTest,
	"Cortex.Graph.Authoring.Migration.Retire.RetainsLateRetainedProducerExecConsumer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireLateRetainedProducerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireLateRetainedProducer")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the late-retained producer chain is created"), Fixture.AddLateRetainedProducerChain());
	if (!Fixture.RetainedProducer || !Fixture.RetainedProducerConsumer) { Fixture.Cleanup(); return false; }

	const FString ProducerGuidText = Fixture.RetainedProducerGuid.ToString();
	const FString ConsumerGuidText = Fixture.RetainedProducerConsumerGuid.ToString();
	const FString AlphaGuidText = Fixture.Alpha->NodeGuid.ToString();
	const FString BetaGuidText = Fixture.Beta->NodeGuid.ToString();

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("retirement preview succeeds: %s"), *Error.ErrorMessage),
		Plan(Fixture, { AlphaGuidText, BetaGuidText }, PlanValue, bReused, Error));

	auto IsShared = [&PlanValue](const FString& GuidText)
	{
		return PlanValue.Shared.ContainsByPredicate([&GuidText](const FCortexGraphPruneNode& Node)
			{ return Node.NodeGuid == GuidText; });
	};
	auto IsBlocked = [&PlanValue](const FString& GuidText)
	{
		return PlanValue.Blocked.ContainsByPredicate([&GuidText](const FCortexGraphPruneNode& Node)
			{ return Node.NodeGuid == GuidText; });
	};
	auto IsRetained = [&IsShared, &IsBlocked](const FString& GuidText)
	{
		return IsShared(GuidText) || IsBlocked(GuidText);
	};
	TestTrue(TEXT("the data-shared producer is retained rather than removable"),
		!PlanValue.RemovableGuids.Contains(ProducerGuidText) && IsShared(ProducerGuidText));
	TestTrue(TEXT("the selected entry feeding the retained producer is blocked"), IsBlocked(AlphaGuidText));

	TestFalse(TEXT("the execution consumer of the retained producer is not removable"),
		PlanValue.RemovableGuids.Contains(ConsumerGuidText));
	TestTrue(TEXT("the retained execution consumer is published as retained, never removable"),
		IsRetained(ConsumerGuidText));
	TestFalse(TEXT("the retained producer -> consumer execution link is not a boundary edge a removal severs"),
		PlanValue.ExternalEdges.ContainsByPredicate([&ProducerGuidText, &ConsumerGuidText](const FCortexGraphPruneEdge& Edge)
		{
			return Edge.FromGuid == ProducerGuidText && Edge.FromPin == TEXT("then")
				&& Edge.ToGuid == ConsumerGuidText && Edge.ToPin == TEXT("execute");
		}));

	TestTrue(TEXT("the independent selected entry stays removable"), PlanValue.RemovableGuids.Contains(BetaGuidText));
	TestTrue(TEXT("the independent selected entry body stays removable"),
		PlanValue.RemovableGuids.Contains(Fixture.BetaBody->NodeGuid.ToString()));

	// The caller-visible inventory must publish the same fixed point: the consumer is never offered.
	const TSharedPtr<FJsonObject> Inventory = FCortexGraphMigrationOps::MakeRetirementInventory(PlanValue.ToJson());
	TestNotNull(TEXT("the retirement inventory is created"), Inventory.Get());
	if (Inventory.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Removable = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Shared = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* BlockedNodes = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* ExternalEdges = nullptr;
		TestTrue(TEXT("the inventory publishes its removable set"),
			Inventory->TryGetArrayField(TEXT("removable"), Removable) && Removable);
		TestTrue(TEXT("the inventory publishes its shared set"),
			Inventory->TryGetArrayField(TEXT("shared"), Shared) && Shared);
		TestTrue(TEXT("the inventory publishes its blocked set"),
			Inventory->TryGetArrayField(TEXT("blocked_nodes"), BlockedNodes) && BlockedNodes);
		TestTrue(TEXT("the inventory publishes its boundary edges"),
			Inventory->TryGetArrayField(TEXT("external_edges"), ExternalEdges) && ExternalEdges);
		if (Removable && Shared && BlockedNodes && ExternalEdges)
		{
			auto NamesGuid = [&ConsumerGuidText](const TArray<TSharedPtr<FJsonValue>>& Values)
			{
				return Values.ContainsByPredicate([&ConsumerGuidText](const TSharedPtr<FJsonValue>& Value)
					{ return Value->AsString().StartsWith(ConsumerGuidText); });
			};
			TestFalse(TEXT("the inventory never offers the retained execution consumer for approval"),
				Removable->ContainsByPredicate([&ConsumerGuidText](const TSharedPtr<FJsonValue>& Value)
					{ return Value->AsString() == ConsumerGuidText; }));
			TestTrue(TEXT("the inventory names the retained execution consumer as retained"),
				NamesGuid(*Shared) || NamesGuid(*BlockedNodes));
			TestFalse(TEXT("the inventory publishes no boundary edge for the retained execution link"),
				ExternalEdges->ContainsByPredicate([&ProducerGuidText, &ConsumerGuidText](const TSharedPtr<FJsonValue>& Value)
					{ return Value->AsString() == FString::Printf(TEXT("%s.then -> %s.execute"), *ProducerGuidText, *ConsumerGuidText); }));
		}
	}

	// Fail closed: the reviewed request may not approve a cut that severs the retained link.
	TSharedPtr<FJsonObject> Request;
	TArray<FString> Approved;
	FCortexCommandResult ReviewError;
	TestFalse(TEXT("the reviewed retirement refuses instead of severing the retained execution link"),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107601"), Request, Approved, ReviewError));
	TestTrue(TEXT("the reviewed refusal is diagnostic"), !ReviewError.ErrorMessage.IsEmpty());
	// The approved set is the preview's removable set, which excludes the retained selected entry Alpha, so
	// the guard that refuses first is the approval-inclusion guard (`approved_node_guids must include
	// selected entry '<Alpha>'`, CortexGraphMigrationOps.cpp:7111) rather than the later blocked/retained
	// refusal (:7278). Naming Alpha as the selected entry its approved set could not include is therefore the
	// honest refusal reason, and it cannot be produced by a malformed request: an invalid patch_id names
	// neither a selected entry nor a node GUID.
	TestTrue(TEXT("the reviewed refusal names the selected entry its approved set could not include"),
		ReviewError.ErrorMessage.Contains(TEXT("selected entry"))
			&& ReviewError.ErrorMessage.Contains(AlphaGuidText));

	// The independent entry is then retired for real: a separately reviewed Beta-only request agrees on
	// exactly its own removable pair, applies it, and the retained producer, its execution consumer and
	// the execution link between them have to survive the applied removal.
	const FString BetaBodyGuidText = Fixture.BetaBody->NodeGuid.ToString();
	TSharedPtr<FJsonObject> IndependentRequest;
	TArray<FString> IndependentApproved;
	FCortexCommandResult IndependentError;
	TestTrue(FString::Printf(TEXT("the independent reviewed request is prepared: %s"), *IndependentError.ErrorMessage),
		PrepareRetirementRequest(Fixture, { BetaGuidText }, TEXT("00000000-0000-0000-0000-000000107602"),
			IndependentRequest, IndependentApproved, IndependentError));
	TestTrue(TEXT("the independent request approves exactly its own removable pair"),
		IndependentApproved.Contains(BetaGuidText) && IndependentApproved.Contains(BetaBodyGuidText)
			&& !IndependentApproved.Contains(ProducerGuidText) && !IndependentApproved.Contains(ConsumerGuidText));

	FCortexGraphPatchOutcome Outcome;
	TestTrue(FString::Printf(TEXT("the reviewed independent retirement applies: %s [%s]"),
		*IndependentError.ErrorMessage, *FString::Join(Outcome.Diagnostics, TEXT("; "))),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, IndependentRequest, Outcome, IndependentError));
	TestEqual(TEXT("the independent retirement readback matches"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestNull(TEXT("the independent entry is absent after the applied retirement"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.BetaGuid));
	TestNull(TEXT("the independent entry body is absent after the applied retirement"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.BetaBodyGuid));
	UEdGraphNode* SurvivorProducer = FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.RetainedProducerGuid);
	UEdGraphNode* SurvivorConsumer = FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.RetainedProducerConsumerGuid);
	TestNotNull(TEXT("the retained producer survives the independent retirement"), SurvivorProducer);
	TestNotNull(TEXT("the retained execution consumer survives the independent retirement"), SurvivorConsumer);
	if (SurvivorProducer && SurvivorConsumer)
	{
		UEdGraphPin* ProducerThen = SurvivorProducer->FindPin(TEXT("then"));
		UEdGraphPin* ConsumerExecute = SurvivorConsumer->FindPin(TEXT("execute"));
		TestTrue(TEXT("the retained producer -> consumer execution link survives the applied retirement"),
			ProducerThen && ConsumerExecute && ProducerThen->LinkedTo.Contains(ConsumerExecute));
		UEdGraphPin* ProducerReturn = SurvivorProducer->FindPin(TEXT("ReturnValue"));
		UEdGraphPin* RetainedBodyInput = Fixture.RetainedBody->FindPin(TEXT("InString"));
		TestTrue(TEXT("the retained producer still feeds the retained cosmetic body"),
			ProducerReturn && RetainedBodyInput && ProducerReturn->LinkedTo.Contains(RetainedBodyInput));
	}

	Fixture.Cleanup();
	return true;
}

// Same partition: a data cycle between removable producers, all of it inside the scanned island. The
// fixed-point walk must terminate on the cycle and keep every member removable instead of refusing or
// classifying the cycle as unproven ownership.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireRemovableDataCycleTest,
	"Cortex.Graph.Authoring.Migration.Retire.BoundedRemovableDataCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireRemovableDataCycleTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireRemovableDataCycle")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the removable data cycle is created"), Fixture.AddRemovableDataCycle());
	if (!Fixture.CycleFirst || !Fixture.CycleSecond) { Fixture.Cleanup(); return false; }

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("retirement preview terminates on the cycle: %s"), *Error.ErrorMessage),
		Plan(Fixture, { Fixture.Alpha->NodeGuid.ToString(), Fixture.Beta->NodeGuid.ToString() },
			PlanValue, bReused, Error));
	TestTrue(TEXT("the bounded cycle traversal reports a complete partition"), PlanValue.bComplete);
	TestTrue(TEXT("the bounded cycle traversal counts the work it examined"),
		PlanValue.ScannedNodes > 0 && PlanValue.ScannedLinks > 0);
	TestTrue(TEXT("the first cycle member is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.CycleFirstGuid.ToString()));
	TestTrue(TEXT("the second cycle member is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.CycleSecondGuid.ToString()));
	TestTrue(TEXT("the whole cyclic island is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.Alpha->NodeGuid.ToString())
			&& PlanValue.RemovableGuids.Contains(Fixture.AlphaBody->NodeGuid.ToString())
			&& PlanValue.RemovableGuids.Contains(Fixture.Producer->NodeGuid.ToString())
			&& PlanValue.RemovableGuids.Contains(Fixture.Beta->NodeGuid.ToString())
			&& PlanValue.RemovableGuids.Contains(Fixture.BetaBody->NodeGuid.ToString()));
	TestTrue(TEXT("the cycle retains and blocks nothing inside the island"),
		PlanValue.Shared.Num() == 0 && PlanValue.Blocked.Num() == 0);

	Fixture.Cleanup();
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireBlockedEntryTest,
	"Cortex.Graph.Authoring.Migration.Retire.BlocksSelectedEntryFeedingRetainedLogic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireBlockedEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireBlocked"), false, true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString AlphaGuid = Fixture.Alpha->NodeGuid.ToString();
	FCortexGraphMigrationRetirePlan PlanValue;

	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("partition-only preview includes blockers: %s"), *Error.ErrorMessage),
		Plan(Fixture, { AlphaGuid }, PlanValue, bReused, Error));
	TestTrue(TEXT("selected entry feeding retained logic is blocked"), PlanValue.Blocked.ContainsByPredicate(
		[&](const FCortexGraphPruneNode& Node) { return Node.NodeGuid == AlphaGuid; }));
	TestFalse(TEXT("blocked selected entry is not removable"), PlanValue.RemovableGuids.Contains(AlphaGuid));
	Error = FCortexCommandResult();
	TestFalse(TEXT("reviewed request refuses blockers"), Plan(Fixture, { AlphaGuid }, PlanValue, bReused, Error, true, { AlphaGuid }));
	TestTrue(TEXT("review refusal is diagnostic"), !Error.ErrorMessage.IsEmpty());
	Fixture.Cleanup();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireStrictShapeTest,
	"Cortex.Graph.Authoring.Migration.Retire.StrictSourceAndEntryShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireStrictShapeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireStrict")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestFalse(TEXT("empty selected entry set is refused"),
		FCortexGraphMigrationOps::PlanRetirement(Fixture.Blueprint, Fixture.Migration({}), PlanValue, bReused, Error));
	Error = FCortexCommandResult();
	const FString AlphaGuid = Fixture.Alpha->NodeGuid.ToString();
	TestFalse(TEXT("duplicate selected GUID is refused"),
		Plan(Fixture, { AlphaGuid, AlphaGuid }, PlanValue, bReused, Error));

	Error = FCortexCommandResult();
	TSharedPtr<FJsonObject> WrongGraph = Fixture.Migration({ AlphaGuid });
	const TSharedPtr<FJsonObject>* Source = nullptr;
	WrongGraph->TryGetObjectField(TEXT("source"), Source);
	const TSharedPtr<FJsonObject>* GraphRef = nullptr;
	(*Source)->TryGetObjectField(TEXT("graph_ref"), GraphRef);
	(*GraphRef)->SetStringField(TEXT("graph_guid"), FGuid::NewGuid().ToString());
	TestFalse(TEXT("wrong graph identity is refused"),
		FCortexGraphMigrationOps::PlanRetirement(Fixture.Blueprint, WrongGraph, PlanValue, bReused, Error));

	auto AddUnsupportedNode = [&Fixture](UEdGraphNode* Node)
	{
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		Fixture.Graph->AddNode(Node, true, false);
		return Node->NodeGuid.ToString();
	};
	Error = FCortexCommandResult();
	UK2Node_CustomEvent* Custom = NewObject<UK2Node_CustomEvent>(Fixture.Graph);
	Custom->CustomFunctionName = TEXT("OnLegacyAlpha");
	Custom->EventReference.SetExternalMember(TEXT("OnLegacyAlpha"), UCortexGraphRetireLegacyWidget::StaticClass());
	Custom->bOverrideFunction = true;
	const FString CustomGuid = AddUnsupportedNode(Custom);
	TestFalse(TEXT("override-shaped custom event is refused"),
		Plan(Fixture, { CustomGuid }, PlanValue, bReused, Error));

	Error = FCortexCommandResult();
	UK2Node_FunctionEntry* FunctionEntry = NewObject<UK2Node_FunctionEntry>(Fixture.Graph);
	const FString FunctionEntryGuid = AddUnsupportedNode(FunctionEntry);
	TestFalse(TEXT("function entry is refused"),
		Plan(Fixture, { FunctionEntryGuid }, PlanValue, bReused, Error));

	Error = FCortexCommandResult();
	UK2Node_FunctionResult* FunctionResult = NewObject<UK2Node_FunctionResult>(Fixture.Graph);
	const FString FunctionResultGuid = AddUnsupportedNode(FunctionResult);
	TestFalse(TEXT("function result is refused"),
		Plan(Fixture, { FunctionResultGuid }, PlanValue, bReused, Error));

	Error = FCortexCommandResult();
	UK2Node_CustomEvent* DuplicateOwner = NewObject<UK2Node_CustomEvent>(Fixture.Graph);
	DuplicateOwner->NodeGuid = Fixture.Alpha->NodeGuid;
	Fixture.Graph->AddNode(DuplicateOwner, true, false);
	TestFalse(TEXT("duplicate GUID ownership is refused"),
		Plan(Fixture, { AlphaGuid }, PlanValue, bReused, Error));

	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireNestedGraphTest,
	"Cortex.Graph.Authoring.Migration.Retire.RefusesNestedCompositeGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireNestedGraphTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireNested")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;

	UK2Node_Composite* Composite = NewObject<UK2Node_Composite>(Fixture.Graph);
	Composite->CreateNewGuid();
	Fixture.Graph->AddNode(Composite, true, false);
	Composite->PostPlacedNewNode();
	UEdGraph* ChildGraph = Composite->BoundGraph;
	TestNotNull(TEXT("a real nested composite graph is created"), ChildGraph);
	if (!ChildGraph) { Fixture.Cleanup(); return false; }

	UK2Node_Event* ChildEntry = NewObject<UK2Node_Event>(ChildGraph);
	ChildEntry->EventReference.SetExternalMember(TEXT("OnLegacyAlpha"), UCortexGraphRetireLegacyWidget::StaticClass());
	ChildEntry->bOverrideFunction = true;
	ChildEntry->CreateNewGuid();
	ChildEntry->AllocateDefaultPins();
	ChildGraph->AddNode(ChildEntry, true, false);

	TSharedPtr<FJsonObject> Migration = Fixture.Migration({ ChildEntry->NodeGuid.ToString() });
	const TSharedPtr<FJsonObject>* Source = nullptr;
	Migration->TryGetObjectField(TEXT("source"), Source);
	const TSharedPtr<FJsonObject>* GraphRef = nullptr;
	(*Source)->TryGetObjectField(TEXT("graph_ref"), GraphRef);
	(*GraphRef)->SetStringField(TEXT("graph_guid"), ChildGraph->GraphGuid.ToString());
	TestFalse(TEXT("nested graph GUID with empty subgraph path is refused"),
		FCortexGraphMigrationOps::PlanRetirement(Fixture.Blueprint, Migration, PlanValue, bReused, Error));
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireUnsupportedBodyTest,
	"Cortex.Graph.Authoring.Migration.Retire.RefusesBoundAndLatentBody",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireUnsupportedBodyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireUnsupportedBody")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	Fixture.Alpha->bInternalEvent = true;
	TestFalse(TEXT("bound/internal event is refused"),
		Plan(Fixture, { Fixture.Alpha->NodeGuid.ToString() }, PlanValue, bReused, Error));
	Fixture.Alpha->bInternalEvent = false;

	UK2Node_CallFunction* Delay = Fixture.AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("Delay")));
	const UEdGraphSchema* Schema = Fixture.Graph->GetSchema();
	TestTrue(TEXT("latent delay is attached to the selected body"),
		Schema->TryCreateConnection(Fixture.AlphaBody->FindPin(TEXT("then")), Delay->FindPin(TEXT("execute"))));
	TestTrue(TEXT("partition preview identifies latent body node"),
		Plan(Fixture, { Fixture.Alpha->NodeGuid.ToString() }, PlanValue, bReused, Error));
	TestTrue(TEXT("latent delay is blocked"), PlanValue.Blocked.ContainsByPredicate(
		[&](const FCortexGraphPruneNode& Node) { return Node.NodeGuid == Delay->NodeGuid.ToString(); }));
	Error = FCortexCommandResult();
	const TArray<FString> Approved = PlanValue.RemovableGuids;
	TestFalse(TEXT("reviewed retirement refuses the latent body blocker"),
		Plan(Fixture, { Fixture.Alpha->NodeGuid.ToString() }, PlanValue, bReused, Error, true, Approved));
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetirePatchEligibilityTest,
	"Cortex.Graph.Authoring.Migration.Retire.PatchEligibilityAndStrictPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetirePatchEligibilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetirePatchEligibility")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	Fixture.Blueprint->Status = BS_Error;
	FCortexCommandResult Error;
	const TSharedPtr<FJsonObject> RetirementMigration = Fixture.Migration({ Fixture.Alpha->NodeGuid.ToString() });
	const TSharedPtr<FJsonObject> RetireRequest = MakeShared<FJsonObject>();
	RetireRequest->SetObjectField(TEXT("migration"), RetirementMigration);
	TestTrue(TEXT("retire_entries is eligible for a compiler-error Blueprint"),
		FCortexGraphPatchOps::ValidateEligibility(Fixture.Blueprint, RetireRequest, Error));
	Error = FCortexCommandResult();
	const TSharedPtr<FJsonObject> PruneRequest = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> PruneMigration = MakeShared<FJsonObject>();
	PruneMigration->SetStringField(TEXT("op"), TEXT("prune_island"));
	PruneRequest->SetObjectField(TEXT("migration"), PruneMigration);
	TestFalse(TEXT("prune_island remains ineligible for a compiler-error Blueprint"),
		FCortexGraphPatchOps::ValidateEligibility(Fixture.Blueprint, PruneRequest, Error));
	Error = FCortexCommandResult();
	const TSharedPtr<FJsonObject> AuthoringRequest = MakeShared<FJsonObject>();
	TestFalse(TEXT("ordinary authoring remains ineligible for a compiler-error Blueprint"),
		FCortexGraphPatchOps::ValidateEligibility(Fixture.Blueprint, AuthoringRequest, Error));

	TSharedPtr<FJsonObject> Malformed = MakeShared<FJsonObject>();
	Malformed->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	Malformed->SetStringField(TEXT("patch_id"), FGuid::NewGuid().ToString());
	Malformed->SetObjectField(TEXT("expected_fingerprint"),
		FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint));
	Malformed->SetObjectField(TEXT("migration"), RetirementMigration);
	TArray<TSharedPtr<FJsonValue>> Nodes;
	Nodes.Add(MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	Malformed->SetArrayField(TEXT("nodes"), Nodes);
	FCortexGraphPreparedPatch Prepared;
	Error = FCortexCommandResult();
	const FString BeforeHash = FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint)
		->GetStringField(TEXT("graph_authoring_hash"));
	const int32 BeforeNodeCount = Fixture.Graph->Nodes.Num();
	TestFalse(TEXT("retirement with mixed authoring nodes is refused"),
		FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Malformed, Prepared, Error));
	TestEqual(TEXT("mixed authoring fields report INVALID_FIELD"), Error.ErrorCode, FString(TEXT("INVALID_FIELD")));
	TestEqual(TEXT("malformed retirement does not change the graph hash"),
		FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint)->GetStringField(TEXT("graph_authoring_hash")),
		BeforeHash);
	TestEqual(TEXT("malformed retirement does not change the graph node count"),
		Fixture.Graph->Nodes.Num(), BeforeNodeCount);
	Fixture.Cleanup();
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireFailureAtomicityTest,
	"Cortex.Graph.Authoring.Migration.Retire.FailureAtomicity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireFailureAtomicityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	struct FFaultCase
	{
		const TCHAR* Name;
		bool bReadback;
	};
	const FFaultCase Faults[] = {
		{ TEXT("migration_retire_after_first_removal"), false },
		{ TEXT("migration_retire_after_removals"), false },
		{ TEXT("retire_after_removal"), true },
		{ TEXT("retire_after_preservation"), true }
	};
	bool bAllPassed = true;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Faults); ++Index)
	{
		FFixture Fixture;
		const FString AssetName = FString::Printf(TEXT("BP_RetireAtomic_%d"), Index);
		bAllPassed &= TestTrue(TEXT("retirement fault fixture is created"), Fixture.Build(*AssetName, true));
		if (!Fixture.Blueprint) { Fixture.Cleanup(); continue; }
		const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
		const bool bDirtyBefore = Fixture.Package->IsDirty();
		TSharedPtr<FJsonObject> Request;
		TArray<FString> Approved;
		FCortexCommandResult Error;
		const FString PatchId = FString::Printf(TEXT("00000000-0000-0000-0000-00000010731%d"), Index);
		if (!PrepareApprovedRequest(Fixture, *PatchId, Request, Approved, Error))
		{
			bAllPassed &= TestFalse(FString::Printf(TEXT("%s approved preflight failed: %s"), Faults[Index].Name,
				*Error.ErrorMessage), true);
			Fixture.Cleanup();
			continue;
		}
		if (Faults[Index].bReadback)
		{
			FCortexGraphMigrationOps::SetRetirementReadbackFaultForTesting(FName(Faults[Index].Name));
		}
		else
		{
			FCortexGraphPatchOps::SetApplyFaultPointForTesting(FName(Faults[Index].Name));
		}
		FCortexGraphPatchOutcome Outcome;
		Error = FCortexCommandResult();
		bAllPassed &= TestFalse(FString::Printf(TEXT("%s causes Execute to fail"), Faults[Index].Name),
			FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
		FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);
		FCortexGraphMigrationOps::ClearRetirementReadbackFaultForTesting();
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s restores the transaction"), Faults[Index].Name),
			Outcome.RollbackStatus, FString(TEXT("restored")));
		bAllPassed &= TestFalse(FString::Printf(TEXT("%s does not block the asset"), Faults[Index].Name),
			Outcome.bBlocked);
		if (Faults[Index].bReadback)
		{
			bAllPassed &= TestEqual(FString::Printf(TEXT("%s reports failed readback"), Faults[Index].Name),
				Outcome.ReadbackStatus, FString(TEXT("mismatched")));
		}
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s restores the independent native graph snapshot"), Faults[Index].Name),
			CaptureNativeGraph(Fixture.Graph), GraphBefore);
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s restores the dirty baseline"), Faults[Index].Name),
			Fixture.Package->IsDirty(), bDirtyBefore);
		for (const FString& GuidText : Approved)
		{
			FGuid Guid;
			FGuid::Parse(GuidText, Guid);
			bAllPassed &= TestNotNull(FString::Printf(TEXT("%s restores approved GUID %s"), Faults[Index].Name, *GuidText),
				FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Guid));
		}
		Fixture.Cleanup();
	}
	FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);
	FCortexGraphMigrationOps::ClearRetirementReadbackFaultForTesting();
	return bAllPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireApplyReadbackTest,
	"Cortex.Graph.Authoring.Migration.Retire.ApplyReadbackWithoutCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireApplyReadbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("retirement fixture is created"), Fixture.Build(TEXT("BP_RetireApplyReadback"), true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	Request->SetStringField(TEXT("patch_id"), TEXT("00000000-0000-0000-0000-000000107301"));
	Request->SetObjectField(TEXT("expected_fingerprint"), FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint));
	Request->SetArrayField(TEXT("nodes"), {});
	Request->SetArrayField(TEXT("connections"), {});
	Request->SetArrayField(TEXT("pin_updates"), {});
	Request->SetBoolField(TEXT("dry_run"), true);
	Request->SetBoolField(TEXT("compile"), false);
	Request->SetBoolField(TEXT("save"), false);
	Request->SetBoolField(TEXT("allow_noop"), false);
	const FString AlphaGuid = Fixture.Alpha->NodeGuid.ToString();
	const FString BetaGuid = Fixture.Beta->NodeGuid.ToString();
	TSharedPtr<FJsonObject> Migration = Fixture.Migration({ AlphaGuid, BetaGuid });
	Request->SetObjectField(TEXT("migration"), Migration);

	FCortexGraphPreparedPatch Preview;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("retirement preview succeeds: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Preview, Error));
	TArray<FString> Approved;
	const TSharedPtr<FJsonObject> PreviewInventory =
		FCortexGraphMigrationOps::MakeRetirementInventory(Preview.RetirementPlan);
	if (PreviewInventory.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ApprovedValues = nullptr;
		PreviewInventory->TryGetArrayField(TEXT("removable"), ApprovedValues);
		if (ApprovedValues)
		{
			for (const TSharedPtr<FJsonValue>& Value : *ApprovedValues) Approved.Add(Value->AsString());
		}
	}
	TestTrue(TEXT("the reviewed approval includes both selected entries"), Approved.Contains(AlphaGuid) && Approved.Contains(BetaGuid));
	TArray<TSharedPtr<FJsonValue>> ApprovedValues;
	for (const FString& Guid : Approved) ApprovedValues.Add(MakeShared<FJsonValueString>(Guid));
	Migration->SetArrayField(TEXT("approved_node_guids"), ApprovedValues);
	FCortexGraphPreparedPatch Reviewed;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("approved preview succeeds: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Reviewed, Error));
	TSet<FGuid> RetainedGuids = { Fixture.Retained->NodeGuid, Fixture.Producer->NodeGuid, Fixture.RetainedBody->NodeGuid };
	const FString RetainedBefore = CaptureNativeGraph(Fixture.Graph, &RetainedGuids);
	Request->SetBoolField(TEXT("dry_run"), false);
	Request->SetStringField(TEXT("expected_validation_hash"), Reviewed.ValidationHash);

	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("approved retirement applies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("approved retirement reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	for (const FString& GuidText : Approved)
	{
		FGuid Guid;
		FGuid::Parse(GuidText, Guid);
		TestNull(FString::Printf(TEXT("approved node %s is absent from the graph and asset"), *GuidText),
			FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Guid));
	}
	TestNotNull(TEXT("retained event remains"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.Retained->NodeGuid));
	TestNotNull(TEXT("shared producer remains"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.Producer->NodeGuid));
	TestNotNull(TEXT("retained body remains"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.RetainedBody->NodeGuid));
	TestEqual(TEXT("retained event, body and shared producer are unchanged"),
		CaptureNativeGraph(Fixture.Graph, &RetainedGuids), RetainedBefore);
	TestTrue(TEXT("retained body execute link survives"),
		Fixture.Retained->FindPin(TEXT("then"))->LinkedTo.Contains(Fixture.RetainedBody->FindPin(TEXT("execute"))));
	TestTrue(TEXT("shared producer remains linked to retained body"),
		Fixture.Producer->FindPin(TEXT("ReturnValue"))->LinkedTo.Contains(Fixture.RetainedBody->FindPin(TEXT("InString"))));
	TestEqual(TEXT("compile is not requested"), Outcome.CompileStatus, FString(TEXT("not_requested")));
	TestEqual(TEXT("native retirement readback matches"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("retirement requests no target compile"), Outcome.TargetCompileCount, 0);
	TestEqual(TEXT("rollback is not requested"), Outcome.RollbackStatus, FString(TEXT("not_requested")));
	TestTrue(TEXT("the package is dirty after retirement"), Fixture.Package->IsDirty());
	TestNotNull(TEXT("retirement inventory is returned"), Outcome.RetirementInventory.Get());
	const TArray<TSharedPtr<FJsonValue>>* InventoryApproved = nullptr;
	TestTrue(TEXT("inventory reports the exact applied approval"),
		Outcome.RetirementInventory.IsValid()
			&& Outcome.RetirementInventory->TryGetArrayField(TEXT("approved_guids"), InventoryApproved)
			&& InventoryApproved && InventoryApproved->Num() == Approved.Num());
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireCompileRecoveryTest,
	"Cortex.Graph.Authoring.Migration.Retire.CompileRecoveryRejectsGeneratedDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireCompileRecoveryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("collision Widget fixture is created under its legacy parent"),
		Fixture.Build(TEXT("BP_RetireCompileRecovery"), false, false, nullptr, false, true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("legacy-parent baseline is saved"), Fixture.SaveToDisk());
	Fixture.Blueprint->ParentClass = UCortexGraphRetireCollisionTargetWidget::StaticClass();
	FBlueprintEditorUtils::RefreshAllNodes(Fixture.Blueprint);
	Fixture.AddNativeNameCollision();
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
	TestEqual(TEXT("unrelated native collision establishes the original error status"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	TestTrue(TEXT("stale graph entries are created after failed compilation"), Fixture.PopulateGraph(true));
	Fixture.Retained->EventReference.SetExternalMember(TEXT("OnRetainedEvent"), UCortexGraphRetireTargetWidget::StaticClass());
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString GeneratedBefore = FCortexGraphPatchState::ComputeGeneratedStateDigest(Fixture.Blueprint);
	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("approved compile request is prepared: %s"), *Error.ErrorMessage),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107401"),
			Request, Approved, Error, true));
	FOperations Operations;
	Operations.Begin();
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("Pasted node"), EAutomationExpectedErrorFlags::Contains, 2);
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("retirement fails while unrelated compiler collision persists"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("generated state drift keeps recovery blocked"), Error.ErrorCode, FString(TEXT("INVALID_OPERATION")));
	TestEqual(TEXT("rollback refuses a changed generated class"), Outcome.RollbackStatus, FString(TEXT("unverified")));
	TestTrue(TEXT("unverified generated recovery blocks the asset"), Outcome.bBlocked);
	TestEqual(TEXT("the recovery compile retains BS_Error"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	TestNotEqual(TEXT("generated drift is not reported as exact restoration"),
		FCortexGraphPatchState::ComputeGeneratedStateDigest(Fixture.Blueprint), GeneratedBefore);
	TestNotEqual(TEXT("engine mutation during failed recovery is not represented as restored authoring"),
		CaptureNativeGraph(Fixture.Graph), GraphBefore);
	for (const FString& GuidText : Approved)
	{
		FGuid Guid;
		FGuid::Parse(GuidText, Guid);
		TestNotNull(TEXT("every approved GUID is restored"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Guid));
	}
	TestTrue(TEXT("blocked failed recovery remains dirty"), Fixture.Package->IsDirty());
	TestEqual(TEXT("failed apply does not save"), Operations.Saves, 0);
	Operations.End();
	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireCompileRecoveryExactMatchTest,
	"Cortex.Graph.Authoring.Migration.Retire.CompileRecoveryAcceptsExactErrorState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireCompileRecoveryExactMatchTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("legacy Widget fixture includes the selected override entries"),
		Fixture.Build(TEXT("BP_RetireCompileRecoveryExact"), true, false, nullptr, false, false));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	Fixture.AddNativeNameCollision();
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	TestTrue(TEXT("legacy-parent entries and the unrelated compile error are saved"), Fixture.SaveToDisk());
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
	TestEqual(TEXT("saved legacy-parent fixture has the real unrelated compile error"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	TestTrue(TEXT("legacy override nodes survive their valid-parent compile"), Fixture.RefreshPointers());

	Fixture.Blueprint->ParentClass = UCortexGraphRetireCollisionTargetWidget::StaticClass();
	FBlueprintEditorUtils::RefreshAllNodes(Fixture.Blueprint);
	TestTrue(TEXT("event nodes remain override nodes after reparent refresh"),
		Fixture.RefreshPointers() && Fixture.Alpha->bOverrideFunction && !Fixture.Alpha->IsA<UK2Node_CustomEvent>()
			&& Fixture.Beta->bOverrideFunction && !Fixture.Beta->IsA<UK2Node_CustomEvent>());
	FBPVariableDescription& CollisionVariable = Fixture.Blueprint->NewVariables.AddDefaulted_GetRef();
	CollisionVariable.VarName = FName(TEXT("NativeCollision"));
	CollisionVariable.VarType.PinCategory = UEdGraphSchema_K2::PC_Int;
	Fixture.Package->MarkPackageDirty();
	Fixture.Blueprint->Status = BS_Error;
	Fixture.Retained->EventReference.SetExternalMember(TEXT("OnRetainedEvent"), UCortexGraphRetireTargetWidget::StaticClass());

	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString GeneratedBefore = FCortexGraphPatchState::ComputeGeneratedStateDigest(Fixture.Blueprint);
	const bool bDirtyBefore = Fixture.Package->IsDirty();
	TestEqual(TEXT("the pre-request generated digest is captured exactly"),
		FCortexGraphPatchState::ComputeGeneratedStateDigest(Fixture.Blueprint), GeneratedBefore);
	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("approved compile request is prepared: %s"), *Error.ErrorMessage),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107405"),
			Request, Approved, Error, true));

	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOps::SetApplyFaultPointForTesting(TEXT("retirement_compile_result_failure"));
	FCortexGraphPatchOutcome Outcome;
	TestFalse(TEXT("injected compile-result failure enters recovery"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	FCortexGraphPatchOps::ClearApplyFaultPointForTesting();

	TestEqual(TEXT("target compile fails once"), Outcome.TargetCompileCount, 1);
	TestEqual(TEXT("coordinator observes one target compile"), Operations.TargetCompiles, 1);
	TestEqual(TEXT("failed recovery compile runs once"), Outcome.RecoveryCompileCount, 1);
	TestEqual(TEXT("coordinator observes one recovery compile"), Operations.RecoveryCompiles, 1);
	TestEqual(TEXT("compile failure remains the operation result"), Error.ErrorCode, FString(TEXT("COMPILE_FAILED")));
	TestEqual(TEXT("exact error-state restoration verifies"), Outcome.RollbackStatus, FString(TEXT("restored")));
	TestFalse(TEXT("exact error-state restoration does not block the asset"), Outcome.bBlocked);
	TestEqual(TEXT("BS_Error is restored exactly"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	TestEqual(TEXT("generated state digest is identical"),
		FCortexGraphPatchState::ComputeGeneratedStateDigest(Fixture.Blueprint), GeneratedBefore);
	TestEqual(TEXT("authoring graph is restored exactly"), CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("dirty baseline is restored"), Fixture.Package->IsDirty(), bDirtyBefore);
	TestEqual(TEXT("failed recovery does not save"), Operations.Saves, 0);
	Operations.End();
	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireCompileOnceTest,
	"Cortex.Graph.Authoring.Migration.Retire.CompileOnceAfterReparent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireCompileOnceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created under its legacy parent"),
		Fixture.Build(TEXT("BP_RetireCompileOnce"), false, false, nullptr, false, true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("legacy-parent Widget is saved before reparenting"), Fixture.SaveToDisk());
	Fixture.Blueprint->ParentClass = UCortexGraphRetireTargetWidget::StaticClass();
	FBlueprintEditorUtils::RefreshAllNodes(Fixture.Blueprint);
	UK2Node_CustomEvent* NativeNameCollision = Fixture.AddNativeNameCollision();
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
	TestEqual(TEXT("baseline compiler collision establishes BS_Error status"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	Fixture.Graph->RemoveNode(NativeNameCollision);
	TestTrue(TEXT("stale graph entries are created after baseline compilation"), Fixture.PopulateGraph(true));
	Fixture.Retained->EventReference.SetExternalMember(TEXT("OnRetainedEvent"), UCortexGraphRetireTargetWidget::StaticClass());
	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("approved compile-once request is prepared: %s"), *Error.ErrorMessage),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107402"),
			Request, Approved, Error, true));
	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOutcome Outcome;
	TestTrue(FString::Printf(TEXT("retirement compiles and verifies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("successful retirement target compile count is one"), Outcome.TargetCompileCount, 1);
	TestEqual(TEXT("one real target compile is observed"), Operations.TargetCompiles, 1);
	TestEqual(TEXT("successful retirement does not recovery compile"), Operations.RecoveryCompiles, 0);
	TestEqual(TEXT("compile status is compiled"), Outcome.CompileStatus, FString(TEXT("compiled")));
	TestEqual(TEXT("readback status is matched"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("successful retirement does not save"), Operations.Saves, 0);
	for (const FString& GuidText : Approved)
	{
		FGuid Guid;
		FGuid::Parse(GuidText, Guid);
		TestNull(TEXT("retired entry/body/producer is absent"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Guid));
	}
	TestNotNull(TEXT("retained override remains"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.Retained->NodeGuid));
	TestNotNull(TEXT("retained body remains"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.RetainedBody->NodeGuid));
	Operations.End();
	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireStagedBytesTest,
	"Cortex.Graph.Authoring.Migration.Retire.StagedApplyPreservesDiskBytes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireStagedBytesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireStagedBytes"), false, false, nullptr, false, true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("clean baseline package is saved"), Fixture.SaveToDisk());
	const TArray<uint8> DiskBefore = ReadBytes(Filename);
	Fixture.AddNativeNameCollision();
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
	TestEqual(TEXT("in-memory package starts invalid"), static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	Fixture.Blueprint->ParentClass = UCortexGraphRetireCollisionTargetWidget::StaticClass();
	FBlueprintEditorUtils::RefreshAllNodes(Fixture.Blueprint);
	FBPVariableDescription& CollisionVariable = Fixture.Blueprint->NewVariables.AddDefaulted_GetRef();
	CollisionVariable.VarName = FName(TEXT("NativeCollision"));
	CollisionVariable.VarType.PinCategory = UEdGraphSchema_K2::PC_Int;
	Fixture.Package->MarkPackageDirty();
	TestTrue(TEXT("stale graph entries are created after failed compilation"), Fixture.PopulateGraph(false));
	Fixture.Blueprint->Status = BS_Error;
	Fixture.Retained->EventReference.SetExternalMember(TEXT("OnRetainedEvent"), UCortexGraphRetireTargetWidget::StaticClass());
	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("staged request is prepared: %s"), *Error.ErrorMessage),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107403"),
			Request, Approved, Error, false, false));
	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOutcome Outcome;
	TestTrue(FString::Printf(TEXT("staged retirement applies and reads back: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("staged compile status is not_requested"), Outcome.CompileStatus, FString(TEXT("not_requested")));
	TestEqual(TEXT("staged readback matches"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestFalse(TEXT("staged operation does not claim save"), Outcome.bSaved);
	TestEqual(TEXT("staged operation does not compile"), Operations.TargetCompiles, 0);
	TestEqual(TEXT("staged operation does not save"), Operations.Saves, 0);
	TestTrue(TEXT("staged operation leaves package dirty"), Fixture.Package->IsDirty());
	TestEqual(TEXT("staged mutation leaves the compiler status dirty"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Dirty));
	TestTrue(TEXT("cached compiler information does not claim a successful compile"),
		Outcome.CompileStatus != TEXT("compiled"));
	TestTrue(TEXT("on-disk bytes are unchanged"), SameBytes(DiskBefore, ReadBytes(Filename)));
	Operations.End();
	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireSaveCleanStartTest,
	"Cortex.Graph.Authoring.Migration.Retire.SaveAfterVerifiedCleanStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireSaveCleanStartTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireSaveClean"), false, false, nullptr, false, true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("legacy-parent baseline saves"), Fixture.SaveToDisk());
	Fixture.Blueprint->ParentClass = UCortexGraphRetireTargetWidget::StaticClass();
	FBlueprintEditorUtils::RefreshAllNodes(Fixture.Blueprint);
	UK2Node_CustomEvent* NativeNameCollision = Fixture.AddNativeNameCollision();
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);
	TestEqual(TEXT("invalid reparented package is BS_Error before recovery"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	Fixture.Graph->RemoveNode(NativeNameCollision);
	TestTrue(TEXT("stale graph entries are created after baseline compilation"), Fixture.PopulateGraph(true));
	Fixture.Retained->EventReference.SetExternalMember(TEXT("OnRetainedEvent"), UCortexGraphRetireTargetWidget::StaticClass());
	TestTrue(TEXT("invalid reparented state is persisted as a clean starting package"), Fixture.SaveToDisk());
	TestFalse(TEXT("package begins clean"), Fixture.Package->IsDirty());
	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("save request is prepared: %s"), *Error.ErrorMessage),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107404"),
			Request, Approved, Error, true, true));
	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOutcome Outcome;
	TestTrue(FString::Printf(TEXT("verified retirement saves: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("post-save status is saved"), Outcome.SaveStatus, FString(TEXT("saved")));
	TestEqual(TEXT("post-save verification is verified"), Outcome.PostSaveStatus, FString(TEXT("verified")));
	TestTrue(TEXT("outcome reports saved"), Outcome.bSaved);
	TestEqual(TEXT("one target compile precedes save"), Operations.TargetCompiles, 1);
	TestEqual(TEXT("one explicit package save occurs"), Operations.Saves, 1);
	TestFalse(TEXT("verified saved package is clean"), Fixture.Package->IsDirty());
	const FString PackageName = Fixture.Package->GetName();
	const FString ObjectName = Fixture.Blueprint->GetName();
	const FGuid GraphGuid = Fixture.Graph->GraphGuid;
	const FGuid RetainedGuid = Fixture.Retained->NodeGuid;
	const FGuid RetainedBodyGuid = Fixture.RetainedBody->NodeGuid;
	const FGuid ProducerGuid = Fixture.Producer->NodeGuid;
	const TArray<FString> RetiredGuidTexts = Approved;
	UPackage* const PackageBeforeReload = Fixture.Package;
	UBlueprint* const BlueprintBeforeReload = Fixture.Blueprint;
	Operations.End();

	TArray<UPackage*> PackagesToReload;
	PackagesToReload.Add(PackageBeforeReload);
	FText ReloadError;
	const bool bReloaded = UPackageTools::ReloadPackages(
		PackagesToReload, ReloadError, EReloadPackagesInteractionMode::AssumeNegative);
	TestTrue(FString::Printf(TEXT("saved retirement package reloads: %s"), *ReloadError.ToString()), bReloaded);
	UPackage* ReloadedPackage = FindPackage(nullptr, *PackageName);
	UWidgetBlueprint* Reloaded = ReloadedPackage
		? FindObject<UWidgetBlueprint>(ReloadedPackage, *ObjectName) : nullptr;
	TestNotNull(TEXT("saved Widget Blueprint resolves after reload"), Reloaded);
	if (Reloaded)
	{
		TestTrue(TEXT("reload replaced the in-memory Blueprint instance"), Reloaded != BlueprintBeforeReload);
		TArray<UEdGraph*> ReloadedGraphs;
		Reloaded->GetAllGraphs(ReloadedGraphs);
		UEdGraph* ReloadedGraph = nullptr;
		for (UEdGraph* Candidate : ReloadedGraphs)
		{
			if (Candidate && Candidate->GraphGuid == GraphGuid)
			{
				ReloadedGraph = Candidate;
				break;
			}
		}
		TestNotNull(TEXT("retained event graph resolves from disk"), ReloadedGraph);
		auto FindNodeInGraph = [](UEdGraph* Graph, const FGuid& Guid) -> UEdGraphNode*
		{
			if (!Graph) return nullptr;
			for (UEdGraphNode* Node : Graph->Nodes) if (Node && Node->NodeGuid == Guid) return Node;
			return nullptr;
		};
		UEdGraphNode* ReloadedRetained = FindNodeInGraph(ReloadedGraph, RetainedGuid);
		UEdGraphNode* ReloadedRetainedBody = FindNodeInGraph(ReloadedGraph, RetainedBodyGuid);
		UEdGraphNode* ReloadedProducer = FindNodeInGraph(ReloadedGraph, ProducerGuid);
		TestNotNull(TEXT("retained event survives in the reloaded graph"), ReloadedRetained);
		TestNotNull(TEXT("retained body survives in the reloaded graph"), ReloadedRetainedBody);
		TestNotNull(TEXT("shared producer survives in the reloaded graph"), ReloadedProducer);
		for (const FString& GuidText : RetiredGuidTexts)
		{
			FGuid Guid;
			FGuid::Parse(GuidText, Guid);
			TestNull(TEXT("retired GUID is absent from the reloaded asset"),
				FCortexGraphMigrationOps::FindNodeByGuid(Reloaded, Guid));
		}
		if (ReloadedRetained && ReloadedRetainedBody && ReloadedProducer)
		{
			UEdGraphPin* RetainedThen = ReloadedRetained->FindPin(TEXT("then"));
			UEdGraphPin* RetainedBodyExecute = ReloadedRetainedBody->FindPin(TEXT("execute"));
			UEdGraphPin* ProducerOutput = ReloadedProducer->FindPin(TEXT("ReturnValue"));
			UEdGraphPin* RetainedBodyInput = ReloadedRetainedBody->FindPin(TEXT("InString"));
			TestNotNull(TEXT("reloaded retained entry has its exec pin"), RetainedThen);
			TestNotNull(TEXT("reloaded retained body has its exec input"), RetainedBodyExecute);
			TestNotNull(TEXT("reloaded producer has its output pin"), ProducerOutput);
			TestNotNull(TEXT("reloaded retained body has its data input"), RetainedBodyInput);
			if (RetainedThen && RetainedBodyExecute)
			{
				TestTrue(TEXT("retained entry-to-body link survives the disk reload"),
					RetainedThen->LinkedTo.Contains(RetainedBodyExecute));
			}
			if (ProducerOutput && RetainedBodyInput)
			{
				TestTrue(TEXT("retained producer-to-body link survives the disk reload"),
					ProducerOutput->LinkedTo.Contains(RetainedBodyInput));
			}
		}
	}

	if (GEditor && GEditor->Trans)
	{
		GEditor->Trans->Reset(FText::FromString(TEXT("CortexGraphMigrationRetireSaveReloadCleanup")));
	}
	UPackage* PackageToCleanup = ReloadedPackage ? ReloadedPackage : PackageBeforeReload;
	if (PackageToCleanup)
	{
		PackageToCleanup->ClearFlags(RF_Standalone);
		PackageToCleanup->MarkAsGarbage();
		ResetLoaders(PackageToCleanup);
	}
	Fixture.Package = nullptr;
	Fixture.Blueprint = nullptr;
	FlushAsyncLoading();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestTrue(TEXT("reloaded retirement fixture file is removed"),
		IFileManager::Get().Delete(*Filename, false, true, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireSaveDirtyStartTest,
	"Cortex.Graph.Authoring.Migration.Retire.RefusesDirtyStartSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireSaveDirtyStartTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("Widget fixture is created"), Fixture.Build(TEXT("BP_RetireSaveDirty"), true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("baseline package is saved"), Fixture.SaveToDisk());
	const TArray<uint8> DiskBefore = ReadBytes(Filename);
	Fixture.Retained->NodeComment = TEXT("unrelated retained edit");
	FBlueprintEditorUtils::MarkBlueprintAsModified(Fixture.Blueprint);
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("dirty-start save request is prepared: %s"), *Error.ErrorMessage),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107405"),
			Request, Approved, Error, true, true));
	FCortexGraphPatchOutcome Outcome;
	TestFalse(TEXT("dirty-start save is refused"), FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("dirty-start refusal reports DIRTY_EDITOR_STATE"), Error.ErrorCode, FString(TEXT("DIRTY_EDITOR_STATE")));
	TestEqual(TEXT("preflight refusal has no target compile"), Outcome.TargetCompileCount, 0);
	TestEqual(TEXT("dirty start has no recovery compile"), Outcome.RecoveryCompileCount, 0);
	TestEqual(TEXT("dirty edit remains in memory"), CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestTrue(TEXT("dirty baseline remains dirty"), Fixture.Package->IsDirty());
	TestTrue(TEXT("disk bytes remain unchanged"), SameBytes(DiskBefore, ReadBytes(Filename)));
	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireAbsentSourceReplayTest,
	"Cortex.Graph.Authoring.Migration.Retire.AbsentSourceReplayIsUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireAbsentSourceReplayTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("retirement fixture is created"), Fixture.Build(TEXT("BP_RetireReplay"), true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	TSharedPtr<FJsonObject> InitialRequest;
	TArray<FString> Approved;
	FCortexCommandResult Error;
	TestTrue(TEXT("initial reviewed request is prepared"),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107501"),
			InitialRequest, Approved, Error));
	FCortexGraphPatchOutcome InitialOutcome;
	TestTrue(FString::Printf(TEXT("initial retirement applies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, InitialRequest, InitialOutcome, Error));
	TestEqual(TEXT("initial retirement applies"), InitialOutcome.ApplyStatus, FString(TEXT("applied")));

	const FString GraphAfterRetirement = CaptureNativeGraph(Fixture.Graph);
	const TSharedPtr<FJsonObject> FingerprintAfterRetirement =
		FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint);
	const FString FingerprintHashAfterRetirement =
		FingerprintAfterRetirement->GetStringField(TEXT("graph_authoring_hash"));
	const bool bDirtyAfterRetirement = Fixture.Package->IsDirty();
	const int32 QueueBeforeReplay = GEditor->Trans->GetQueueLength();
	const int32 UndoBeforeReplay = GEditor->Trans->GetUndoCount();
	const int32 NodeCountBeforeReplay = Fixture.Graph->Nodes.Num();
	TSharedPtr<FJsonObject> ReplayRequest = MakeShared<FJsonObject>();
	ReplayRequest->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	ReplayRequest->SetStringField(TEXT("patch_id"), TEXT("00000000-0000-0000-0000-000000107501"));
	ReplayRequest->SetObjectField(TEXT("expected_fingerprint"), FingerprintAfterRetirement);
	ReplayRequest->SetArrayField(TEXT("nodes"), {});
	ReplayRequest->SetArrayField(TEXT("connections"), {});
	ReplayRequest->SetArrayField(TEXT("pin_updates"), {});
	ReplayRequest->SetBoolField(TEXT("dry_run"), true);
	ReplayRequest->SetBoolField(TEXT("compile"), false);
	ReplayRequest->SetBoolField(TEXT("save"), false);
	ReplayRequest->SetBoolField(TEXT("allow_noop"), false);
	ReplayRequest->SetObjectField(TEXT("migration"), Fixture.Migration(
		{ Fixture.AlphaGuid.ToString(), Fixture.BetaGuid.ToString() }, true, Approved));

	FCortexGraphPreparedPatch ReplayPreview;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("fresh approved replay preview succeeds: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Preflight(Fixture.Blueprint, ReplayRequest, ReplayPreview, Error));
	TestTrue(TEXT("replay preview reuses the retirement plan"), ReplayPreview.RetirementPlan.IsValid());
	TestFalse(TEXT("replay preview reports no graph change"), ReplayPreview.bChanged);
	TestTrue(TEXT("replay preview records absent-source provenance"), ReplayPreview.bReplayedWithAbsentSource);
	TestTrue(TEXT("replay preview carries a fresh validation hash"),
		!ReplayPreview.ValidationHash.IsEmpty()
			&& ReplayPreview.ValidationHash != InitialRequest->GetStringField(TEXT("expected_validation_hash")));
	if (ReplayPreview.RetirementPlan.IsValid())
	{
		const TSharedPtr<FJsonObject> Inventory =
			FCortexGraphMigrationOps::MakeRetirementInventory(ReplayPreview.RetirementPlan);
		TestTrue(TEXT("replay is marked reused"), Inventory.IsValid() && Inventory->GetBoolField(TEXT("reused")));
		TestTrue(TEXT("replay inventory is complete"), Inventory.IsValid() && Inventory->GetBoolField(TEXT("complete")));
		TestFalse(TEXT("replay inventory is not awaiting approval"),
			Inventory.IsValid() && Inventory->GetBoolField(TEXT("awaiting_approval")));
	}
	ReplayRequest->SetBoolField(TEXT("dry_run"), false);
	ReplayRequest->SetStringField(TEXT("expected_validation_hash"), ReplayPreview.ValidationHash);

	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOutcome ReplayOutcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("freshly reviewed replay applies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, ReplayRequest, ReplayOutcome, Error));
	Operations.End();
	TestEqual(TEXT("replay reports unchanged"), ReplayOutcome.ApplyStatus, FString(TEXT("unchanged")));
	TestFalse(TEXT("replay reports no graph change"), ReplayOutcome.bChanged);
	TestTrue(TEXT("replay outcome reports reused retirement inventory"),
		ReplayOutcome.RetirementInventory.IsValid()
			&& ReplayOutcome.RetirementInventory->GetBoolField(TEXT("reused")));
	TestTrue(TEXT("replay records absent-source provenance"), ReplayOutcome.bReplayedWithAbsentSource);
	TestEqual(TEXT("replay creates no transaction"), GEditor->Trans->GetQueueLength(), QueueBeforeReplay);
	TestEqual(TEXT("replay creates no undo record"), GEditor->Trans->GetUndoCount(), UndoBeforeReplay);
	TestEqual(TEXT("replay requests no target compile"), Operations.TargetCompiles, 0);
	TestEqual(TEXT("replay requests no recovery compile"), Operations.RecoveryCompiles, 0);
	TestEqual(TEXT("replay does not save"), Operations.Saves, 0);
	TestFalse(TEXT("replay does not claim save"), ReplayOutcome.bSaved);
	TestEqual(TEXT("replay preserves graph state"), CaptureNativeGraph(Fixture.Graph), GraphAfterRetirement);
	TestEqual(TEXT("replay preserves graph node count"), Fixture.Graph->Nodes.Num(), NodeCountBeforeReplay);
	TestEqual(TEXT("replay preserves fingerprint"), FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint)
		->GetStringField(TEXT("graph_authoring_hash")), FingerprintHashAfterRetirement);
	TestEqual(TEXT("replay preserves package dirty state"), Fixture.Package->IsDirty(), bDirtyAfterRetirement);
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetirePartialReplayRefusalTest,
	"Cortex.Graph.Authoring.Migration.Retire.PartialReplayNamesAbsentAndPresentIdentities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetirePartialReplayRefusalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("retirement fixture is created"), Fixture.Build(TEXT("BP_RetirePartialReplay"), true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	TSharedPtr<FJsonObject> InitialRequest;
	TArray<FString> Approved;
	FCortexCommandResult Error;
	TestTrue(TEXT("initial reviewed request is prepared"),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107502"),
			InitialRequest, Approved, Error));
	FCortexGraphPatchOutcome Outcome;
	TestTrue(TEXT("initial retirement applies"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, InitialRequest, Outcome, Error));

	UK2Node_CustomEvent* Reintroduced = NewObject<UK2Node_CustomEvent>(Fixture.Graph);
	Reintroduced->NodeGuid = FGuid(Approved[0]);
	Fixture.Graph->AddNode(Reintroduced, true, false);
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	Error = FCortexCommandResult();
	TestFalse(TEXT("partial postcondition is refused"),
		Plan(Fixture, { Fixture.AlphaGuid.ToString(), Fixture.BetaGuid.ToString() },
			PlanValue, bReused, Error, true, Approved));
	TestEqual(TEXT("partial replay reports INVALID_OPERATION"), Error.ErrorCode, FString(TEXT("INVALID_OPERATION")));
	TArray<FString> ExpectedAbsent = Approved;
	ExpectedAbsent.RemoveAt(0);
	ExpectedAbsent.Sort();
	const FString ExpectedPartialDiagnostic = FString::Printf(
		TEXT("retirement replay is partial: %d approved identities are absent [%s] and 1 are present [%s]"),
		Approved.Num() - 1, *FString::Join(ExpectedAbsent, TEXT(", ")), *Approved[0]);
	TestEqual(TEXT("partial replay reports exact absent and present counts and identities"),
		Error.ErrorMessage, ExpectedPartialDiagnostic);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireOwnershipConflictTest,
	"Cortex.Graph.Authoring.Migration.Retire.AbsentSourceOwnershipConflictIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireOwnershipConflictTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FFixture Fixture;
	TestTrue(TEXT("retirement fixture is created"), Fixture.Build(TEXT("BP_RetireOwnershipConflict"), true));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	TSharedPtr<FJsonObject> InitialRequest;
	TArray<FString> Approved;
	FCortexCommandResult Error;
	TestTrue(TEXT("initial reviewed request is prepared"),
		PrepareApprovedRequest(Fixture, TEXT("00000000-0000-0000-0000-000000107503"),
			InitialRequest, Approved, Error));
	FCortexGraphPatchOutcome Outcome;
	TestTrue(TEXT("initial retirement applies"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, InitialRequest, Outcome, Error));

	UEdGraph* OtherGraph = NewObject<UEdGraph>(Fixture.Blueprint);
	OtherGraph->GraphGuid = FGuid::NewGuid();
	Fixture.Blueprint->FunctionGraphs.Add(OtherGraph);
	UK2Node_CustomEvent* Reowned = NewObject<UK2Node_CustomEvent>(OtherGraph);
	Reowned->NodeGuid = FGuid(Approved[0]);
	OtherGraph->AddNode(Reowned, true, false);
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	Error = FCortexCommandResult();
	TestFalse(TEXT("approved absent GUID owned by another graph is refused"),
		Plan(Fixture, { Fixture.AlphaGuid.ToString(), Fixture.BetaGuid.ToString() },
			PlanValue, bReused, Error, true, Approved));
	TestEqual(TEXT("ownership conflict reports INVALID_OPERATION"), Error.ErrorCode, FString(TEXT("INVALID_OPERATION")));
	TestTrue(TEXT("ownership conflict names the conflicting GUID and graph"),
		Error.ErrorMessage.Contains(Approved[0]) && Error.ErrorMessage.Contains(OtherGraph->GraphGuid.ToString()));
	Fixture.Cleanup();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireDiagnosticsTruncationTest,
	"Cortex.Graph.Authoring.Migration.Retire.DiagnosticsTruncationIsTruthful",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireDiagnosticsTruncationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;

	// Case A: Long cached diagnostic (> 512 chars)
	{
		FFixture Fixture;
		TestTrue(TEXT("Case A fixture created"), Fixture.Build(TEXT("BP_RetireDiagLong")));
		if (Fixture.Blueprint)
		{
			Fixture.Alpha->bHasCompilerMessage = true;
			Fixture.Alpha->ErrorMsg = FString::ChrN(700, TEXT('x'));

			FCortexGraphMigrationRetirePlan PlanValue;
			bool bReused = false;
			FCortexCommandResult Error;
			TestTrue(TEXT("preview succeeds with long diagnostic"),
				Plan(Fixture, { Fixture.AlphaGuid.ToString(), Fixture.BetaGuid.ToString() }, PlanValue, bReused, Error));
			TestEqual(TEXT("exactly one cached diagnostic collected"), PlanValue.PreexistingDiagnostics.Num(), 1);
			if (PlanValue.PreexistingDiagnostics.Num() == 1)
			{
				TestTrue(TEXT("returned string respects 512 character bound"), PlanValue.PreexistingDiagnostics[0].Len() <= 512);
			}
			TestTrue(TEXT("preexisting_diagnostics_truncated is true for character truncation"),
				PlanValue.bPreexistingDiagnosticsTruncated);

			const TSharedPtr<FJsonObject> Inventory = FCortexGraphMigrationOps::MakeRetirementInventory(PlanValue.ToJson());
			TestNotNull(TEXT("inventory is valid"), Inventory.Get());
			if (Inventory.IsValid())
			{
				TestTrue(TEXT("inventory reports preexisting_diagnostics_truncated true"),
					Inventory->GetBoolField(TEXT("preexisting_diagnostics_truncated")));
				TestEqual(TEXT("inventory names cached_node_messages source"),
					Inventory->GetStringField(TEXT("preexisting_diagnostics_source")), FString(TEXT("cached_node_messages")));
			}
			Fixture.Cleanup();
		}
	}

	// Case B: Count truncation (> 16 messages)
	{
		FFixture Fixture;
		TestTrue(TEXT("Case B fixture created"), Fixture.Build(TEXT("BP_RetireDiagCount")));
		if (Fixture.Blueprint)
		{
			for (int32 Index = 0; Index < 20; ++Index)
			{
				UK2Node_CallFunction* Dummy = Fixture.AddCall(UKismetSystemLibrary::StaticClass()->FindFunctionByName(TEXT("PrintString")));
				Dummy->bHasCompilerMessage = true;
				Dummy->ErrorMsg = FString::Printf(TEXT("error message %d"), Index);
			}

			FCortexGraphMigrationRetirePlan PlanValue;
			bool bReused = false;
			FCortexCommandResult Error;
			TestTrue(TEXT("preview succeeds with many diagnostics"),
				Plan(Fixture, { Fixture.AlphaGuid.ToString(), Fixture.BetaGuid.ToString() }, PlanValue, bReused, Error));
			TestTrue(TEXT("diagnostics count bounded by 16"), PlanValue.PreexistingDiagnostics.Num() <= 16);
			TestTrue(TEXT("omission marker is present"),
				PlanValue.PreexistingDiagnostics.Contains(TEXT("additional compiler diagnostics omitted")));
			TestTrue(TEXT("preexisting_diagnostics_truncated is true for count truncation"),
				PlanValue.bPreexistingDiagnosticsTruncated);

			const TSharedPtr<FJsonObject> Inventory = FCortexGraphMigrationOps::MakeRetirementInventory(PlanValue.ToJson());
			TestNotNull(TEXT("inventory is valid"), Inventory.Get());
			if (Inventory.IsValid())
			{
				TestTrue(TEXT("inventory reports preexisting_diagnostics_truncated true for count truncation"),
					Inventory->GetBoolField(TEXT("preexisting_diagnostics_truncated")));
			}
			Fixture.Cleanup();
		}
	}

	// Case C: No truncation (single short message)
	{
		FFixture Fixture;
		TestTrue(TEXT("Case C fixture created"), Fixture.Build(TEXT("BP_RetireDiagShort")));
		if (Fixture.Blueprint)
		{
			Fixture.Alpha->bHasCompilerMessage = true;
			Fixture.Alpha->ErrorMsg = TEXT("short compiler warning message");

			FCortexGraphMigrationRetirePlan PlanValue;
			bool bReused = false;
			FCortexCommandResult Error;
			TestTrue(TEXT("preview succeeds with short diagnostic"),
				Plan(Fixture, { Fixture.AlphaGuid.ToString(), Fixture.BetaGuid.ToString() }, PlanValue, bReused, Error));
			TestEqual(TEXT("exactly one cached diagnostic collected"), PlanValue.PreexistingDiagnostics.Num(), 1);
			TestFalse(TEXT("preexisting_diagnostics_truncated is false when no truncation occurs"),
				PlanValue.bPreexistingDiagnosticsTruncated);

			const TSharedPtr<FJsonObject> Inventory = FCortexGraphMigrationOps::MakeRetirementInventory(PlanValue.ToJson());
			TestNotNull(TEXT("inventory is valid"), Inventory.Get());
			if (Inventory.IsValid())
			{
				TestFalse(TEXT("inventory reports preexisting_diagnostics_truncated false when no truncation"),
					Inventory->GetBoolField(TEXT("preexisting_diagnostics_truncated")));
			}
			Fixture.Cleanup();
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireConformEngineTransitionTest,
	"Cortex.Graph.Authoring.Migration.Retire.PostReparentCompileConformsStaleOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireConformEngineTransitionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;

	FFixture Fixture;
	TestTrue(TEXT("Build legacy parent widget fixture"),
		Fixture.Build(TEXT("BP_RetireEngineTransition"), false, false, UCortexGraphRetireLegacyWidget::StaticClass()));
	if (!Fixture.Blueprint) return false;

	// 1. Before compile / reparent: genuine eligible override event
	TestNotNull(TEXT("Alpha exists before reparent"), Fixture.Alpha);
	TestTrue(TEXT("Alpha is UK2Node_Event before reparent"), Fixture.Alpha != nullptr && !Fixture.Alpha->IsA<UK2Node_CustomEvent>());
	if (Fixture.Alpha)
	{
		TestTrue(TEXT("Alpha is an override function before reparent"), Fixture.Alpha->bOverrideFunction);
		TestEqual(TEXT("Alpha member name is OnLegacyAlpha"),
			Fixture.Alpha->EventReference.GetMemberName(), FName(TEXT("OnLegacyAlpha")));
	}
	const FGuid OriginalAlphaGuid = Fixture.AlphaGuid;

	// 2. Reparent to target widget (where OnLegacyAlpha does not exist)
	Fixture.Blueprint->ParentClass = UCortexGraphRetireCollisionTargetWidget::StaticClass();
	FBlueprintEditorUtils::RefreshAllNodes(Fixture.Blueprint);

	// 3. Add an independent deterministic defect causing compile failure
	Fixture.AddNativeNameCollision();

	// 4. Compile: Blueprint enters real BS_Error
	// In UE 5.8, ConformImplementedEvents emits a warning for each stale override conformed to custom event
	AddExpectedError(TEXT("replaced as a Custom Event"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("name conflicts with a native"), EAutomationExpectedErrorFlags::Contains, 1);
	FKismetEditorUtilities::CompileBlueprint(Fixture.Blueprint);

	TestEqual(TEXT("Blueprint entered real BS_Error status"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));

	// 5. In UE 5.8, FBlueprintEditorUtils::ConformImplementedEvents destroys stale override UK2Node_Event
	// and substitutes a UK2Node_CustomEvent that preserves the original NodeGuid
	UK2Node_CustomEvent* SubstituteCustomEvent = nullptr;
	for (UEdGraphNode* Node : Fixture.Graph->Nodes)
	{
		if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
		{
			if (CustomEvent->CustomFunctionName.ToString().Contains(TEXT("OnLegacyAlpha")))
			{
				SubstituteCustomEvent = CustomEvent;
				break;
			}
		}
	}
	TestNotNull(TEXT("stale override node was conformed into a UK2Node_CustomEvent by UE 5.8 compiler"),
		SubstituteCustomEvent);
	if (SubstituteCustomEvent)
	{
		TestEqual(TEXT("substitute custom event inherits the original node GUID"),
			SubstituteCustomEvent->NodeGuid, OriginalAlphaGuid);
		TestTrue(TEXT("substitute node is a UK2Node_CustomEvent"),
			SubstituteCustomEvent->IsA<UK2Node_CustomEvent>());
		TestFalse(TEXT("substitute node no longer has bOverrideFunction"),
			SubstituteCustomEvent->bOverrideFunction);
	}

	// 6. CortexSandbox #112 classifies the conformed node by its current shape: it is now an
	// unreferenced custom event, so the guarded contract accepts it instead of pinning the legacy
	// refusal that only described the old override-only predicate.
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const TArray<FString> Selected = { OriginalAlphaGuid.ToString() };
	const bool bAccepted = Plan(Fixture, Selected, PlanValue, bReused, Error);
	TestTrue(FString::Printf(TEXT("retire_entries accepts the conformed unreferenced custom event: %s"), *Error.ErrorMessage), bAccepted);
	if (bAccepted)
	{
		TestTrue(TEXT("the conformed custom event entry is removable"),
			PlanValue.RemovableGuids.Contains(OriginalAlphaGuid.ToString()));
		TestTrue(TEXT("a custom-event entry publishes that a compile is required for its cleanup"),
			PlanValue.bRequiresCompile);
		TestEqual(TEXT("the plan publishes exactly one entry"), PlanValue.Entries.Num(), 1);
		if (PlanValue.Entries.Num() == 1)
		{
			TestEqual(TEXT("the conformed entry is classified as a custom event by its current node shape"),
				PlanValue.Entries[0].Kind, FString(TEXT("custom_event")));
			TestTrue(TEXT("the conformed entry carries its real generated function name"),
				PlanValue.Entries[0].CustomFunctionName.Contains(TEXT("OnLegacyAlpha")));
		}
	}

	// 7. A live caller makes the very same node non-retirable, because removing it would sever the
	// dispatch the asset really relies on.
	if (SubstituteCustomEvent && bAccepted)
	{
		Error = FCortexCommandResult();
		UK2Node_CallFunction* Caller = NewObject<UK2Node_CallFunction>(Fixture.Graph);
		Caller->FunctionReference.SetSelfMember(FName(*SubstituteCustomEvent->CustomFunctionName.ToString()));
		Caller->CreateNewGuid();
		Caller->AllocateDefaultPins();
		Fixture.Graph->AddNode(Caller, true, false);
		TestFalse(TEXT("retire_entries refuses a custom event with an in-asset caller"),
			Plan(Fixture, Selected, PlanValue, bReused, Error));
		TestEqual(TEXT("caller refusal error code is INVALID_OPERATION"), Error.ErrorCode, FString(TEXT("INVALID_OPERATION")));
		TestTrue(TEXT("the refusal names the in-asset caller"),
			Error.ErrorMessage.Contains(TEXT("still referenced in this asset")));
	}

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: a component-bound delegate entry is a genuine event island, not an
// unsupported node. Before the guarded extension `retire_entries` refused it as "not a supported
// unbound override event", so this test fails on the old entry eligibility.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundEntryTest,
	"Cortex.Graph.Authoring.Migration.Retire.SupportsComponentBoundDelegateEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("Widget fixture with a compiled component-bound delegate is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundEntry")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the guarded fixture compiles without a Blueprint error"),
		static_cast<int32>(Fixture.Blueprint->Status) != static_cast<int32>(BS_Error));

	// Genuine identity: the compiled widget variable, the native delegate and the binding function.
	TestEqual(TEXT("component-bound entry binds the compiled widget variable"),
		Fixture.Bound->GetComponentPropertyName(), FName(TEXT("ActionButton")));
	TestEqual(TEXT("component-bound entry binds the native OnClicked delegate"),
		Fixture.Bound->DelegatePropertyName, FName(TEXT("OnClicked")));
	TestTrue(TEXT("component delegate owner is the native button class"),
		Fixture.Bound->DelegateOwnerClass.Get() == UButton::StaticClass());
	TestTrue(TEXT("component-bound entry is an internal event node"), Fixture.Bound->bInternalEvent);
	TestTrue(TEXT("component-bound entry is not an unbound override"), !Fixture.Bound->bOverrideFunction);
	TestNotNull(TEXT("the generated binding function exists in the compiled class"),
		Fixture.Blueprint->GeneratedClass
			? Fixture.Blueprint->GeneratedClass->FindFunctionByName(Fixture.Bound->CustomFunctionName) : nullptr);
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	TestTrue(TEXT("the compiled class registers the ActionButton.OnClicked component binding"),
		BindingsBefore.Contains(TEXT("ActionButton|OnClicked")));

	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	const bool bDirtyBefore = Fixture.Package->IsDirty();

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const TArray<FString> Selected = { Fixture.Bound->NodeGuid.ToString() };
	const bool bPreviewed = Plan(Fixture, Selected, PlanValue, bReused, Error);
	TestTrue(FString::Printf(TEXT("retire_entries accepts a component-bound delegate entry: %s"), *Error.ErrorMessage), bPreviewed);
	TestTrue(TEXT("component-bound entry is removable"), PlanValue.RemovableGuids.Contains(Fixture.Bound->NodeGuid.ToString()));
	TestTrue(TEXT("the component-bound entry island body is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.BoundBody->NodeGuid.ToString()));
	TestFalse(TEXT("the unrelated cosmetic override is not removable"),
		PlanValue.RemovableGuids.Contains(Fixture.Retained->NodeGuid.ToString()));
	TestFalse(TEXT("the retained cosmetic override body is not removable"),
		PlanValue.RemovableGuids.Contains(Fixture.RetainedBody->NodeGuid.ToString()));
	TestEqual(TEXT("preview leaves the graph unchanged"), CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("preview leaves the widget tree unchanged"), CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);
	TestEqual(TEXT("preview leaves the component delegate bindings unchanged"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);
	TestEqual(TEXT("preview leaves the dirty state unchanged"), Fixture.Package->IsDirty(), bDirtyBefore);

	// Exact reviewed approval covers the same entry class and still refuses partial approval. The
	// approval phase only runs once the preview produced a removable set, so an unsupported entry
	// class fails on the legible preview assertion above instead of crashing on an empty approval.
	if (bPreviewed && !PlanValue.RemovableGuids.IsEmpty())
	{
		FCortexGraphMigrationRetirePlan ApprovedPlan;
		Error = FCortexCommandResult();
		const TArray<FString> ExactApproval = PlanValue.RemovableGuids;
		const bool bApproved = Plan(Fixture, Selected, ApprovedPlan, bReused, Error, true, ExactApproval);
		TestTrue(FString::Printf(TEXT("reviewed approval accepts the component-bound plan: %s"), *Error.ErrorMessage), bApproved);
		TestTrue(TEXT("reviewed component-bound plan is no longer awaiting approval"), !ApprovedPlan.bAwaitingApproval);
		TestTrue(TEXT("reviewed component-bound plan approves the exact removable set"),
			ApprovedPlan.ApprovedGuids == ApprovedPlan.RemovableGuids);

		FCortexGraphMigrationRetirePlan PartialPlan;
		Error = FCortexCommandResult();
		TArray<FString> PartialApproval = ExactApproval;
		PartialApproval.Pop();
		const bool bPartial = Plan(Fixture, Selected, PartialPlan, bReused, Error, true, PartialApproval);
		TestTrue(FString::Printf(TEXT("partial approval is refused for the component-bound plan: %s"), *Error.ErrorMessage), !bPartial);
	}

	TestNotNull(TEXT("the preserved cosmetic override survives the preview"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.Retained->NodeGuid));
	TestTrue(TEXT("the preserved cosmetic override keeps its execution link"),
		Fixture.Retained->FindPin(TEXT("then"))
			&& Fixture.Retained->FindPin(TEXT("then"))->LinkedTo.Contains(Fixture.RetainedBody->FindPin(TEXT("execute"))));

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: widget Construct/OnInitialized override bodies become obsolete once the
// reparented native parent implements the lifecycle. Before the guarded extension `retire_entries`
// refused every lifecycle member by name, so this test fails on the old entry eligibility.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireWidgetLifecycleEntryTest,
	"Cortex.Graph.Authoring.Migration.Retire.SupportsWidgetLifecycleEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireWidgetLifecycleEntryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("natively reparented Widget fixture is created"), Fixture.Build(TEXT("BP_RetireWidgetLifecycleEntry")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the guarded fixture compiles without a Blueprint error"),
		static_cast<int32>(Fixture.Blueprint->Status) != static_cast<int32>(BS_Error));

	// A plain UUserWidget parent is not proof that native behaviour replaced the retired body.
	TestTrue(TEXT("the lifecycle fixture parent is not plain UUserWidget"),
		Fixture.Blueprint->ParentClass && Fixture.Blueprint->ParentClass != UUserWidget::StaticClass());
	TestTrue(TEXT("the lifecycle fixture parent is a native widget class"),
		Fixture.Blueprint->ParentClass && Fixture.Blueprint->ParentClass->IsChildOf(UUserWidget::StaticClass())
			&& Fixture.Blueprint->ParentClass->HasAnyClassFlags(CLASS_Native));

	TestEqual(TEXT("Construct entry overrides the widget lifecycle declaration"),
		Fixture.Construct->EventReference.GetMemberName(), FName(TEXT("Construct")));
	TestTrue(TEXT("Construct entry is an override event"), Fixture.Construct->bOverrideFunction);
	TestTrue(TEXT("Construct entry is not an internal event"), !Fixture.Construct->bInternalEvent);
	TestTrue(TEXT("Construct entry is a plain event node, not a custom event"), !Fixture.Construct->IsA<UK2Node_CustomEvent>());
	TestEqual(TEXT("OnInitialized entry overrides the widget lifecycle declaration"),
		Fixture.Initialized->EventReference.GetMemberName(), FName(TEXT("OnInitialized")));
	TestTrue(TEXT("OnInitialized entry is an override event"), Fixture.Initialized->bOverrideFunction);
	TestTrue(TEXT("OnInitialized entry is a plain event node, not a custom event"), !Fixture.Initialized->IsA<UK2Node_CustomEvent>());

	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const TArray<FString> Selected = {
		Fixture.Construct->NodeGuid.ToString(), Fixture.Initialized->NodeGuid.ToString() };
	const bool bPreviewed = Plan(Fixture, Selected, PlanValue, bReused, Error);
	TestTrue(FString::Printf(TEXT("retire_entries accepts widget lifecycle entries: %s"), *Error.ErrorMessage), bPreviewed);
	TestTrue(TEXT("Construct entry is removable"), PlanValue.RemovableGuids.Contains(Fixture.Construct->NodeGuid.ToString()));
	TestTrue(TEXT("Construct island body is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.ConstructBody->NodeGuid.ToString()));
	TestTrue(TEXT("OnInitialized entry is removable"), PlanValue.RemovableGuids.Contains(Fixture.Initialized->NodeGuid.ToString()));
	TestTrue(TEXT("OnInitialized island body is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.InitializedBody->NodeGuid.ToString()));
	TestFalse(TEXT("the unrelated cosmetic override is not removable"),
		PlanValue.RemovableGuids.Contains(Fixture.Retained->NodeGuid.ToString()));
	TestEqual(TEXT("preview leaves the graph unchanged"), CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("preview leaves the widget tree unchanged"), CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: an unreferenced custom event is a callable generated function with no caller,
// so retiring its island severs no dispatch. Before the guarded extension `retire_entries` refused
// every UK2Node_CustomEvent, so this test fails on the old entry eligibility.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireUnreferencedCustomEventTest,
	"Cortex.Graph.Authoring.Migration.Retire.SupportsUnreferencedCustomEventEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireUnreferencedCustomEventTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("Widget fixture with an unreferenced custom event is created"),
		Fixture.Build(TEXT("BP_RetireUnreferencedCustomEvent")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the guarded fixture compiles without a Blueprint error"),
		static_cast<int32>(Fixture.Blueprint->Status) != static_cast<int32>(BS_Error));

	TestTrue(TEXT("the custom event entry is not an override event"), !Fixture.Custom->bOverrideFunction);
	TestEqual(TEXT("the custom event entry carries its real custom function name"),
		Fixture.Custom->CustomFunctionName, FName(TEXT("OnLegacyTutorialEvent")));
	TestNotNull(TEXT("the custom event compiled into a callable generated function"),
		Fixture.Blueprint->GeneratedClass
			? Fixture.Blueprint->GeneratedClass->FindFunctionByName(Fixture.Custom->CustomFunctionName) : nullptr);
	int32 InAssetCallers = 0;
	for (const UEdGraphNode* Node : Fixture.Graph->Nodes)
	{
		const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		if (Call && Call->FunctionReference.GetMemberName() == Fixture.Custom->CustomFunctionName) ++InAssetCallers;
	}
	TestEqual(TEXT("the fixture custom event has no in-asset caller"), InAssetCallers, 0);

	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const TArray<FString> Selected = { Fixture.Custom->NodeGuid.ToString() };
	const bool bPreviewed = Plan(Fixture, Selected, PlanValue, bReused, Error);
	TestTrue(FString::Printf(TEXT("retire_entries accepts an unreferenced custom event entry: %s"), *Error.ErrorMessage), bPreviewed);
	TestTrue(TEXT("unreferenced custom event entry is removable"), PlanValue.RemovableGuids.Contains(Fixture.Custom->NodeGuid.ToString()));
	TestTrue(TEXT("the custom event island body is removable"),
		PlanValue.RemovableGuids.Contains(Fixture.CustomBody->NodeGuid.ToString()));
	TestFalse(TEXT("the unrelated cosmetic override is not removable"),
		PlanValue.RemovableGuids.Contains(Fixture.Retained->NodeGuid.ToString()));
	TestEqual(TEXT("preview leaves the graph unchanged"), CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("preview leaves the widget tree unchanged"), CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: the class-specific identity of a component-bound entry is published in the
// plan, so the reviewed approval binds the exact component property, delegate property and generated
// binding function, and every unresolved or ambiguous fact is refused before anything mutates.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundInventoryTest,
	"Cortex.Graph.Authoring.Migration.Retire.ComponentBoundInventoryAndRefusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundInventoryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded component-bound fixture is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundInventory")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FName BindingFunction = Fixture.Bound->CustomFunctionName;
	const FString Selected = Fixture.Bound->NodeGuid.ToString();
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	const bool bPreviewed = Plan(Fixture, { Selected }, PlanValue, bReused, Error);
	TestTrue(FString::Printf(TEXT("component-bound preview succeeds: %s"), *Error.ErrorMessage), bPreviewed);
	if (!bPreviewed) { Fixture.Cleanup(); return false; }

	TestEqual(TEXT("the plan publishes exactly one entry identity"), PlanValue.Entries.Num(), 1);
	if (PlanValue.Entries.Num() == 1)
	{
		const FCortexGraphMigrationRetireEntry& Entry = PlanValue.Entries[0];
		TestEqual(TEXT("the entry is classified as a component-bound event"),
			Entry.Kind, FString(TEXT("component_bound_event")));
		TestEqual(TEXT("the entry binds the compiled widget variable"),
			Entry.ComponentPropertyName, FString(TEXT("ActionButton")));
		TestEqual(TEXT("the entry binds the native OnClicked delegate"),
			Entry.DelegatePropertyName, FString(TEXT("OnClicked")));
		TestTrue(TEXT("the entry names the native delegate owner class"),
			Entry.DelegateOwnerClass.Contains(TEXT("Button")));
		TestEqual(TEXT("the entry names the generated binding function"),
			Entry.BindingFunctionName, BindingFunction.ToString());
	}
	TestTrue(TEXT("a component-bound entry publishes that a compile is required"),
		PlanValue.bRequiresCompile);

	const TSharedPtr<FJsonObject> Inventory = FCortexGraphMigrationOps::MakeRetirementInventory(PlanValue.ToJson());
	const TArray<TSharedPtr<FJsonValue>>* EntryValues = nullptr;
	TArray<FString> EntryLines;
	if (Inventory.IsValid() && Inventory->TryGetArrayField(TEXT("selected_entries"), EntryValues) && EntryValues)
	{
		for (const TSharedPtr<FJsonValue>& Value : *EntryValues) EntryLines.Add(Value->AsString());
	}
	TestEqual(TEXT("the inventory publishes one class-specific entry line"), EntryLines.Num(), 1);
	TestTrue(TEXT("the inventory line names the class, component, delegate and binding function"),
		EntryLines.Num() == 1 && EntryLines[0].Contains(TEXT("component_bound_event"))
			&& EntryLines[0].Contains(TEXT("ActionButton")) && EntryLines[0].Contains(TEXT("OnClicked"))
			&& EntryLines[0].Contains(BindingFunction.ToString()));
	bool bInventoryRequiresCompile = false;
	TestTrue(TEXT("the inventory publishes requires_compile=true"),
		Inventory.IsValid()
			&& Inventory->TryGetBoolField(TEXT("requires_compile"), bInventoryRequiresCompile)
			&& bInventoryRequiresCompile);

	// An unresolved component property is refused instead of being trusted by name.
	const FName BoundComponent = Fixture.Bound->ComponentPropertyName;
	const FName BoundDelegate = Fixture.Bound->DelegatePropertyName;
	Fixture.Bound->ComponentPropertyName = TEXT("MissingWidgetVariable");
	Error = FCortexCommandResult();
	TestFalse(TEXT("an unresolved component property is refused"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestTrue(TEXT("the refusal names the unresolved component property"),
		Error.ErrorMessage.Contains(TEXT("does not resolve to an object property")));
	Fixture.Bound->ComponentPropertyName = BoundComponent;

	// An unresolved delegate is refused as well: a remapped or deleted delegate is never assumed safe.
	Error = FCortexCommandResult();
	Fixture.Bound->DelegatePropertyName = TEXT("OnMissingDelegate");
	TestFalse(TEXT("an unresolved delegate property is refused"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestTrue(TEXT("the refusal names the unresolved delegate"),
		Error.ErrorMessage.Contains(TEXT("does not resolve to a multicast delegate property")));
	Fixture.Bound->DelegatePropertyName = BoundDelegate;

	// An in-asset reference to the generated binding function is refused.
	Error = FCortexCommandResult();
	UK2Node_CallFunction* BindingCaller = NewObject<UK2Node_CallFunction>(Fixture.Graph);
	BindingCaller->FunctionReference.SetSelfMember(BindingFunction);
	BindingCaller->CreateNewGuid();
	BindingCaller->AllocateDefaultPins();
	Fixture.Graph->AddNode(BindingCaller, true, false);
	TestFalse(TEXT("an in-asset reference to the generated binding function is refused"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestTrue(TEXT("the refusal names the in-asset binding reference"),
		Error.ErrorMessage.Contains(TEXT("still reference its generated binding function")));

	// A second event node bound to the same component delegate is an ambiguous duplicate binding. It
	// is created through the same engine initializer the editor uses, so the duplicate is real.
	Error = FCortexCommandResult();
	UK2Node_ComponentBoundEvent* Duplicate = NewObject<UK2Node_ComponentBoundEvent>(Fixture.Graph, NAME_None, RF_Transactional);
	Duplicate->CreateNewGuid();
	Duplicate->InitializeComponentBoundEventParams(Fixture.ComponentProperty, Fixture.ClickedDelegate);
	Fixture.Graph->AddNode(Duplicate, true, false);
	Duplicate->AllocateDefaultPins();
	TestTrue(TEXT("the duplicate node really shares the component delegate pair"),
		Duplicate->ComponentPropertyName == Fixture.Bound->ComponentPropertyName
			&& Duplicate->DelegatePropertyName == Fixture.Bound->DelegatePropertyName
			&& Duplicate->NodeGuid != Fixture.Bound->NodeGuid);
	TestFalse(TEXT("an ambiguous duplicate component binding is refused"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestTrue(TEXT("the refusal reports the ambiguous duplicate binding"),
		Error.ErrorMessage.Contains(TEXT("ambiguous duplicate binding")));

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: the removed component-bound node leaves its generated dynamic binding behind
// until a compile rebuilds the binding array, so a compiled apply must prove the binding and the
// generated function are gone instead of inferring it from the node removal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundCompiledApplyTest,
	"Cortex.Graph.Authoring.Migration.Retire.ComponentBoundCompiledApplyClearsBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundCompiledApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded component-bound fixture is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundCompiled")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FGuid BoundGuid = Fixture.Bound->NodeGuid;
	const FGuid BoundBodyGuid = Fixture.BoundBody->NodeGuid;
	const FName BindingFunction = Fixture.Bound->CustomFunctionName;
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	TestTrue(TEXT("the fixture registers the component binding before retirement"),
		BindingsBefore.Contains(TEXT("ActionButton|OnClicked"))
			&& BindingsBefore.Contains(BindingFunction.ToString()));

	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the guarded apply request prepares: %s"), *Error.ErrorMessage),
		PrepareGuardedRequest(Fixture, { BoundGuid.ToString() }, TEXT("00000000-0000-0000-0000-000000112001"),
			Request, Approved, Error, /*bCompile=*/true, /*bSave=*/false));
	if (!Request.IsValid()) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the reviewed approval covers the entry and its island body"),
		Approved.Contains(BoundGuid.ToString()) && Approved.Contains(BoundBodyGuid.ToString()));

	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("the compiled component-bound retirement applies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("the apply reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("exactly one target compile ran"), Outcome.TargetCompileCount, 1);
	TestEqual(TEXT("the target compiled"), Outcome.CompileStatus, FString(TEXT("compiled")));
	TestEqual(TEXT("the compiled class-specific readback matched"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("no rollback was required"), Outcome.RollbackStatus, FString(TEXT("not_requested")));
	TestNull(TEXT("the retired component-bound node is absent"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, BoundGuid));
	TestNull(TEXT("the retired island body is absent"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, BoundBodyGuid));
	TestNotNull(TEXT("the unrelated cosmetic override survives"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Fixture.Retained->NodeGuid));
	TestEqual(TEXT("the widget tree is unchanged"), CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);

	const FString BindingsAfter = CaptureComponentBindings(Fixture.Blueprint);
	TestFalse(TEXT("the retired generated binding function is gone from the compiled binding array"),
		BindingsAfter.Contains(BindingFunction.ToString()));
	TestFalse(TEXT("the ActionButton.OnClicked binding is gone"), BindingsAfter.Contains(TEXT("ActionButton|OnClicked")));
	TestNull(TEXT("the compiled class no longer declares the retired binding function"),
		Fixture.Blueprint->GeneratedClass ? Fixture.Blueprint->GeneratedClass->FindFunctionByName(BindingFunction) : nullptr);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: `compile=false, save=false` stays an honest staging result. The removal and the
// retained graph are verified, but the stale generated binding is still registered and the response
// says so instead of reporting a runtime-safe outcome it cannot prove.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundStagedApplyTest,
	"Cortex.Graph.Authoring.Migration.Retire.ComponentBoundStagedApplyIsNotRuntimeSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundStagedApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded component-bound fixture is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundStaged")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FGuid BoundGuid = Fixture.Bound->NodeGuid;
	const FName BindingFunction = Fixture.Bound->CustomFunctionName;
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	TestTrue(TEXT("the fixture registers the component binding before retirement"),
		BindingsBefore.Contains(BindingFunction.ToString()));

	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the staged apply request prepares: %s"), *Error.ErrorMessage),
		PrepareGuardedRequest(Fixture, { BoundGuid.ToString() }, TEXT("00000000-0000-0000-0000-000000112002"),
			Request, Approved, Error, /*bCompile=*/false, /*bSave=*/false));
	if (!Request.IsValid()) { Fixture.Cleanup(); return false; }

	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("the staged component-bound retirement applies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("the staged apply reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the staged apply requests no compile"), Outcome.CompileStatus, FString(TEXT("not_requested")));
	TestEqual(TEXT("the staged apply runs no target compile"), Outcome.TargetCompileCount, 0);
	TestEqual(TEXT("the staged native readback matched"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestNull(TEXT("the retired component-bound node is absent from the graph"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, BoundGuid));
	TestTrue(TEXT("the stale generated component binding is still registered after a staged apply"),
		CaptureComponentBindings(Fixture.Blueprint).Contains(BindingFunction.ToString()));
	TestNotNull(TEXT("the stale generated binding function still exists after a staged apply"),
		Fixture.Blueprint->GeneratedClass ? Fixture.Blueprint->GeneratedClass->FindFunctionByName(BindingFunction) : nullptr);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: a custom event compiles into a callable generated function, so it is only
// retirable while nothing in the asset references it and the editor cannot invoke it directly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireCustomEventRefusalTest,
	"Cortex.Graph.Authoring.Migration.Retire.CustomEventRefusesCallerAndCallInEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireCustomEventRefusalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;

	{
		FGuardedEntryFixture Fixture;
		TestTrue(TEXT("guarded custom-event fixture is created"),
			Fixture.Build(TEXT("BP_RetireCustomCaller")));
		if (Fixture.IsComplete())
		{
			const FString Selected = Fixture.Custom->NodeGuid.ToString();
			FCortexGraphMigrationRetirePlan PlanValue;
			bool bReused = false;
			FCortexCommandResult Error;
			TestTrue(TEXT("the unreferenced custom event previews before a caller exists"),
				Plan(Fixture, { Selected }, PlanValue, bReused, Error));

			UK2Node_CallFunction* Caller = NewObject<UK2Node_CallFunction>(Fixture.Graph);
			Caller->FunctionReference.SetSelfMember(Fixture.Custom->CustomFunctionName);
			Caller->CreateNewGuid();
			Caller->AllocateDefaultPins();
			Fixture.Graph->AddNode(Caller, true, false);
			Error = FCortexCommandResult();
			TestFalse(TEXT("a custom event with a live in-asset caller is refused"),
				Plan(Fixture, { Selected }, PlanValue, bReused, Error));
			TestTrue(TEXT("the caller refusal names the referencing node"),
				Error.ErrorMessage.Contains(TEXT("still referenced in this asset"))
					&& Error.ErrorMessage.Contains(Caller->NodeGuid.ToString()));
		}
		Fixture.Cleanup();
	}

	{
		FGuardedEntryFixture Fixture;
		TestTrue(TEXT("guarded call-in-editor fixture is created"),
			Fixture.Build(TEXT("BP_RetireCustomCallInEditor")));
		if (Fixture.IsComplete())
		{
			const FString Selected = Fixture.Custom->NodeGuid.ToString();
			FCortexGraphMigrationRetirePlan PlanValue;
			bool bReused = false;
			FCortexCommandResult Error;
			Fixture.Custom->bCallInEditor = true;
			TestFalse(TEXT("a call-in-editor custom event is refused"),
				Plan(Fixture, { Selected }, PlanValue, bReused, Error));
			TestTrue(TEXT("the refusal explains the editor-callable event"),
				Error.ErrorMessage.Contains(TEXT("callable in the editor")));
			Fixture.Custom->bCallInEditor = false;
			Error = FCortexCommandResult();
			TestTrue(TEXT("the same custom event is retirable once the editor-callable flag is cleared"),
				Plan(Fixture, { Selected }, PlanValue, bReused, Error));
		}
		Fixture.Cleanup();
	}
	return true;
}

// CortexSandbox #112: one request may retire a mixed set of guarded entry classes while the unrelated
// cosmetic override, the retained graph links and the designer widget tree stay provably unchanged.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireMixedGuardedApplyTest,
	"Cortex.Graph.Authoring.Migration.Retire.MixedGuardedApplyPreservesRetainedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireMixedGuardedApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded mixed fixture is created"), Fixture.Build(TEXT("BP_RetireMixedGuarded")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FName BindingFunction = Fixture.Bound->CustomFunctionName;
	const FName CustomFunction = Fixture.Custom->CustomFunctionName;
	const FGuid RetainedGuid = Fixture.Retained->NodeGuid;
	const FGuid RetainedBodyGuid = Fixture.RetainedBody->NodeGuid;
	const TArray<FGuid> RemovedGuids = {
		Fixture.Bound->NodeGuid, Fixture.BoundBody->NodeGuid,
		Fixture.Construct->NodeGuid, Fixture.ConstructBody->NodeGuid,
		Fixture.Initialized->NodeGuid, Fixture.InitializedBody->NodeGuid,
		Fixture.Custom->NodeGuid, Fixture.CustomBody->NodeGuid };
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	TSet<FGuid> RetainedSet = { RetainedGuid, RetainedBodyGuid };
	const FString RetainedBefore = CaptureNativeGraph(Fixture.Graph, &RetainedSet);

	const TArray<FString> Selected = {
		Fixture.Bound->NodeGuid.ToString(), Fixture.Construct->NodeGuid.ToString(),
		Fixture.Initialized->NodeGuid.ToString(), Fixture.Custom->NodeGuid.ToString() };

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the mixed guarded preview succeeds: %s"), *Error.ErrorMessage),
		Plan(Fixture, Selected, PlanValue, bReused, Error));
	TestEqual(TEXT("the plan publishes one identity per selected entry"), PlanValue.Entries.Num(), 4);
	TSet<FString> Kinds;
	for (const FCortexGraphMigrationRetireEntry& Entry : PlanValue.Entries) Kinds.Add(Entry.Kind);
	TestTrue(TEXT("the plan publishes each distinct guarded entry class"),
		Kinds.Contains(FString(TEXT("component_bound_event"))) && Kinds.Contains(FString(TEXT("lifecycle_event")))
			&& Kinds.Contains(FString(TEXT("custom_event"))) && Kinds.Num() == 3);
	TestTrue(TEXT("the mixed plan requires a compile"), PlanValue.bRequiresCompile);

	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("the mixed guarded apply request prepares: %s"), *Error.ErrorMessage),
		PrepareGuardedRequest(Fixture, Selected, TEXT("00000000-0000-0000-0000-000000112003"),
			Request, Approved, Error, /*bCompile=*/true, /*bSave=*/false));
	if (!Request.IsValid()) { Fixture.Cleanup(); return false; }

	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("the mixed guarded retirement applies: %s"), *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("the mixed apply reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the mixed apply compiled once"), Outcome.TargetCompileCount, 1);
	TestEqual(TEXT("the mixed compiled readback matched"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	for (const FGuid& RemovedGuid : RemovedGuids)
	{
		TestNull(FString::Printf(TEXT("retired node %s is absent"), *RemovedGuid.ToString()),
			FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, RemovedGuid));
	}
	TestNotNull(TEXT("the cosmetic override survives"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, RetainedGuid));
	TestNotNull(TEXT("the cosmetic override body survives"), FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, RetainedBodyGuid));
	TestEqual(TEXT("the retained cosmetic island is unchanged"), CaptureNativeGraph(Fixture.Graph, &RetainedSet), RetainedBefore);
	TestEqual(TEXT("the designer widget tree is unchanged"), CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);
	TestFalse(TEXT("the retired component binding is gone from the compiled class"),
		CaptureComponentBindings(Fixture.Blueprint).Contains(BindingFunction.ToString()));
	TestNull(TEXT("the retired custom event function is gone from the compiled class"),
		Fixture.Blueprint->GeneratedClass ? Fixture.Blueprint->GeneratedClass->FindFunctionByName(CustomFunction) : nullptr);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: the removal journal is the recovery contract for the class-specific identity a
// component-bound node carries, so an apply fault must restore the binding identity itself instead of
// relying on an authoring fingerprint that cannot see it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundApplyFaultTest,
	"Cortex.Graph.Authoring.Migration.Retire.ComponentBoundApplyFaultRestoresIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundApplyFaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded component-bound fixture is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundApplyFault")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FGuid BoundGuid = Fixture.Bound->NodeGuid;
	const FName BindingFunction = Fixture.Bound->CustomFunctionName;
	const FName BoundComponent = Fixture.Bound->ComponentPropertyName;
	const FName BoundDelegate = Fixture.Bound->DelegatePropertyName;
	UClass* BoundDelegateOwner = Fixture.Bound->DelegateOwnerClass;
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);

	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the guarded apply request prepares: %s"), *Error.ErrorMessage),
		PrepareGuardedRequest(Fixture, { BoundGuid.ToString() }, TEXT("00000000-0000-0000-0000-000000112004"),
			Request, Approved, Error, /*bCompile=*/false, /*bSave=*/false));
	if (!Request.IsValid()) { Fixture.Cleanup(); return false; }

	FCortexGraphPatchOps::SetApplyFaultPointForTesting(TEXT("migration_retire_after_first_removal"));
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("the injected apply fault fails the retirement"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);
	TestEqual(TEXT("the journal restored the transaction"), Outcome.RollbackStatus, FString(TEXT("restored")));
	TestFalse(TEXT("the restored asset is not blocked"), Outcome.bBlocked);
	UK2Node_ComponentBoundEvent* Restored = Cast<UK2Node_ComponentBoundEvent>(
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, BoundGuid));
	TestNotNull(TEXT("the retired component-bound node was restored"), Restored);
	if (Restored)
	{
		TestEqual(TEXT("the restored node keeps its component property"), Restored->ComponentPropertyName, BoundComponent);
		TestEqual(TEXT("the restored node keeps its delegate property"), Restored->DelegatePropertyName, BoundDelegate);
		TestEqual(TEXT("the restored node keeps its delegate owner"), Restored->DelegateOwnerClass.Get(), BoundDelegateOwner);
		TestEqual(TEXT("the restored node keeps its generated binding function"), Restored->CustomFunctionName, BindingFunction);
	}
	TestEqual(TEXT("the restored graph matches the pre-request snapshot"), CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the generated component binding is untouched by the rolled-back apply"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: a compiled apply whose class-specific readback fails must roll back the removed
// component binding as well, so the new generated-artefact comparison seam is proven recoverable.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundReadbackFaultTest,
	"Cortex.Graph.Authoring.Migration.Retire.ComponentBoundReadbackFaultRollsBackBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundReadbackFaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded component-bound fixture is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundReadbackFault")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FGuid BoundGuid = Fixture.Bound->NodeGuid;
	const FName BindingFunction = Fixture.Bound->CustomFunctionName;
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);

	TArray<FString> Approved;
	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the guarded apply request prepares: %s"), *Error.ErrorMessage),
		PrepareGuardedRequest(Fixture, { BoundGuid.ToString() }, TEXT("00000000-0000-0000-0000-000000112005"),
			Request, Approved, Error, /*bCompile=*/true, /*bSave=*/false));
	if (!Request.IsValid()) { Fixture.Cleanup(); return false; }

	FCortexGraphMigrationOps::SetRetirementReadbackFaultForTesting(TEXT("retire_after_binding"));
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("the injected class-specific readback fault fails the retirement"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	FCortexGraphMigrationOps::ClearRetirementReadbackFaultForTesting();
	TestEqual(TEXT("the class-specific readback reported a mismatch"), Outcome.ReadbackStatus, FString(TEXT("mismatched")));
	TestEqual(TEXT("the journal and the recovery compile restored the transaction"),
		Outcome.RollbackStatus, FString(TEXT("restored")));
	TestFalse(TEXT("the restored asset is not blocked"), Outcome.bBlocked);
	UK2Node_ComponentBoundEvent* Restored = Cast<UK2Node_ComponentBoundEvent>(
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, BoundGuid));
	TestNotNull(TEXT("the compiled retirement was rolled back to the component-bound node"), Restored);
	if (Restored)
	{
		TestEqual(TEXT("the restored node keeps its generated binding function"), Restored->CustomFunctionName, BindingFunction);
	}
	TestTrue(TEXT("the recovery compile registered the component binding again"),
		CaptureComponentBindings(Fixture.Blueprint).Contains(BindingFunction.ToString()));
	TestEqual(TEXT("the rolled-back compiled retirement restores the exact component binding set"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);
	TestNotNull(TEXT("the recovery compile re-declares the binding function"),
		Fixture.Blueprint->GeneratedClass ? Fixture.Blueprint->GeneratedClass->FindFunctionByName(BindingFunction) : nullptr);

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: a plain `UUserWidget` parent is no proof that native behaviour replaced a
// retired lifecycle body, so the lifecycle class is refused for it even though the node shape is a
// legitimate inherited override.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireLifecyclePlainParentTest,
	"Cortex.Graph.Authoring.Migration.Retire.LifecycleEntryRefusesPlainUserWidgetParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireLifecyclePlainParentTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded lifecycle fixture is created"), Fixture.Build(TEXT("BP_RetireLifecyclePlainParent")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const TArray<FString> Selected = {
		Fixture.Construct->NodeGuid.ToString(), Fixture.Initialized->NodeGuid.ToString() };
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(TEXT("the natively reparented lifecycle entries preview before the parent changes"),
		Plan(Fixture, Selected, PlanValue, bReused, Error));

	UClass* const NativeParent = Fixture.Blueprint->ParentClass;
	Fixture.Blueprint->ParentClass = UUserWidget::StaticClass();
	Error = FCortexCommandResult();
	TestFalse(TEXT("a plain UUserWidget parent refuses the lifecycle entries"),
		Plan(Fixture, Selected, PlanValue, bReused, Error));
	TestTrue(TEXT("the refusal explains that the parent is not a native Widget class"),
		Error.ErrorMessage.Contains(TEXT("is not a native Widget class that owns the lifecycle hook")));
	Fixture.Blueprint->ParentClass = NativeParent;

	// The native parent is the only difference, so the identical request is accepted again.
	Error = FCortexCommandResult();
	TestTrue(TEXT("the native parent accepts the same lifecycle entries again"),
		Plan(Fixture, Selected, PlanValue, bReused, Error));

	Fixture.Cleanup();
	return true;
}

// CortexSandbox #112: `UK2Node_CustomEvent` compiles into a BlueprintCallable/Public generated
// function, so a caller in another loaded Blueprint is a real call site. A retirement that can see
// such a caller must refuse it instead of deleting the entry and severing the cross-asset call.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireCustomEventExternalCallerTest,
	"Cortex.Graph.Authoring.Migration.Retire.CustomEventRefusesExternalCaller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireCustomEventExternalCallerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded custom-event fixture is created"),
		Fixture.Build(TEXT("BP_RetireExternalCallerTarget")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FString Selected = Fixture.Custom->NodeGuid.ToString();
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestTrue(TEXT("the custom event is retirable while no external caller exists"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestTrue(TEXT("the unreferenced custom event island is approved for removal"),
		PlanValue.RemovableGuids.Contains(Selected));

	const FString TargetFilename = Fixture.Filename();
	IFileManager::Get().Delete(*TargetFilename, false, true, true);
	TestTrue(TEXT("the target asset is saved before the external caller exists"), Fixture.SaveToDisk());
	const TArray<uint8> TargetBytesBefore = ReadBytes(TargetFilename);

	FExternalCallerFixture External;
	TestTrue(TEXT("a second Widget Blueprint calls the generated custom event function"),
		External.Build(TEXT("BP_RetireExternalCallerHost"), Fixture.Blueprint, Fixture.Custom->CustomFunctionName));
	const FString ExternalFilename = External.Filename();
	if (!External.Caller)
	{
		External.Cleanup();
		Fixture.Cleanup();
		IFileManager::Get().Delete(*TargetFilename, false, true, true);
		return false;
	}
	IFileManager::Get().Delete(*ExternalFilename, false, true, true);
	TestTrue(TEXT("the external caller asset is saved"), External.SaveToDisk());
	const TArray<uint8> ExternalBytesBefore = ReadBytes(ExternalFilename);

	// Every oracle is captured with the external caller already loaded and saved, so only the refused
	// retirement could still change any of them.
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	const bool bDirtyBefore = Fixture.Package->IsDirty();

	Error = FCortexCommandResult();
	TestFalse(TEXT("preview refuses to retire a custom event with a live external caller"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestEqual(TEXT("the external caller refusal is INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
	TestTrue(TEXT("the external caller refusal is diagnostic"), !Error.ErrorMessage.IsEmpty());

	Error = FCortexCommandResult();
	FCortexGraphPreparedPatch Preview;
	TestFalse(TEXT("the patch preview refuses the external caller before any mutation"),
		PreviewRetirement(Fixture, { Selected }, TEXT("00000000-0000-0000-0000-000000112101"), Preview, Error));
	TestEqual(TEXT("the refused patch preview reports INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));

	TestEqual(TEXT("the refused preview leaves the target graph unchanged"),
		CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the refused preview leaves the designer widget tree unchanged"),
		CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);
	TestEqual(TEXT("the refused preview leaves the compiled component bindings unchanged"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);
	TestEqual(TEXT("the refused preview leaves the dirty state unchanged"),
		Fixture.Package->IsDirty(), bDirtyBefore);
	TestNotNull(TEXT("the live external caller is still present in the second asset"),
		FCortexGraphMigrationOps::FindNodeByGuid(External.Blueprint, External.Caller->NodeGuid));
	TestTrue(TEXT("the target asset bytes are unchanged"), SameBytes(TargetBytesBefore, ReadBytes(TargetFilename)));
	TestTrue(TEXT("the external caller asset bytes are unchanged"),
		SameBytes(ExternalBytesBefore, ReadBytes(ExternalFilename)));

	External.Cleanup();
	Fixture.Cleanup();
	IFileManager::Get().Delete(*TargetFilename, false, true, true);
	IFileManager::Get().Delete(*ExternalFilename, false, true, true);
	return true;
}

// CortexSandbox #112 second review: the referencer guard keys on `FindPackage`, so a referencer
// whose package is resident but whose Blueprint asset is not loaded was silently treated as "no
// caller" — the loaded-Blueprint scan cannot see an unloaded asset. Root docs require fail-closed
// on an uninspectable referencer, and a preview must never auto-load one to inspect it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireResidentReferencerTest,
	"Cortex.Graph.Authoring.Migration.Retire.RefusesResidentReferencerWithoutLoadedAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireResidentReferencerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded custom-event fixture is created"),
		Fixture.Build(TEXT("BP_RetireResidentReferencerTarget")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FString Selected = Fixture.Custom->NodeGuid.ToString();
	const FString TargetFilename = Fixture.Filename();
	IFileManager::Get().Delete(*TargetFilename, false, true, true);
	TestTrue(TEXT("the target asset is saved before the referencer exists"), Fixture.SaveToDisk());

	FExternalCallerFixture External;
	const bool bReferencerBuilt = External.Build(TEXT("BP_RetireResidentReferencerHost"),
		Fixture.Blueprint, Fixture.Custom->CustomFunctionName);
	TestTrue(TEXT("a second Widget Blueprint calls the target's generated custom event function"), bReferencerBuilt);
	const FString ExternalFilename = External.Filename();
	const FString ExternalPackageName = External.PackageName();
	const FString ExternalObjectPath = External.ObjectPath();
	if (!bReferencerBuilt)
	{
		External.Cleanup();
		Fixture.Cleanup();
		IFileManager::Get().Delete(*TargetFilename, false, true, true);
		if (!ExternalFilename.IsEmpty()) IFileManager::Get().Delete(*ExternalFilename, false, true, true);
		return false;
	}
	IFileManager::Get().Delete(*ExternalFilename, false, true, true);
	TestTrue(TEXT("the referencer asset is saved"), External.SaveToDisk());
	const TArray<uint8> TargetBytesBefore = ReadBytes(TargetFilename);
	const TArray<uint8> ExternalBytesBefore = ReadBytes(ExternalFilename);

	// The loophole state: the referencer's package stays resident while its Blueprint asset is
	// unloaded, which `FindPackage` alone cannot tell apart from an inspectable referencer.
	TestTrue(TEXT("the referencer package stays resident while its Blueprint asset is unloaded"),
		External.UnloadAssetKeepingPackageResident());
	TestNotNull(TEXT("the referencer package is resident at preview time"), FindPackage(nullptr, *ExternalPackageName));
	TestNull(TEXT("the referencer Blueprint asset is not loaded at preview time"),
		FindObject<UBlueprint>(nullptr, *ExternalObjectPath));
	TestTrue(TEXT("the referencer package still exists on disk"), FPackageName::DoesPackageExist(ExternalPackageName));

	FString RegistryFailure;
	const bool bRegistryKnowsReferencer = ScanSavedReferencers({ TargetFilename, ExternalFilename },
		Fixture.Package->GetFName(), FName(*ExternalPackageName), RegistryFailure);
	TestTrue(FString::Printf(TEXT("fixture precondition: the Asset Registry reports the saved referencer: %s"),
		*RegistryFailure), bRegistryKnowsReferencer);
	if (!bRegistryKnowsReferencer)
	{
		External.Cleanup();
		Fixture.Cleanup();
		IFileManager::Get().Delete(*TargetFilename, false, true, true);
		IFileManager::Get().Delete(*ExternalFilename, false, true, true);
		return false;
	}

	// Only a refused preview can still change any oracle captured below.
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	UPackage* const ResidentReferencerBefore = FindPackage(nullptr, *ExternalPackageName);
	const bool bTargetDirtyBefore = Fixture.Package->IsDirty();
	const bool bReferencerDirtyBefore = ResidentReferencerBefore && ResidentReferencerBefore->IsDirty();

	FOperations Operations;
	Operations.Begin();
	FCortexCommandResult Error;
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	const bool bPlanned = Plan(Fixture, { Selected }, PlanValue, bReused, Error);
	FCortexGraphPreparedPatch Preview;
	Error = FCortexCommandResult();
	const bool bPreviewed = PreviewRetirement(Fixture, { Selected },
		TEXT("00000000-0000-0000-0000-000000112201"), Preview, Error);
	Operations.End();

	TestFalse(TEXT("the preview refuses an uninspectable resident referencer instead of assuming there is no caller"),
		bPlanned);
	TestEqual(TEXT("the uninspectable referencer refusal is INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
	TestTrue(TEXT("the uninspectable referencer refusal names the referencer and why it could not be inspected"),
		ReferencerRefusalIsDiagnostic(Error, ExternalPackageName));
	TestFalse(TEXT("the selected custom event is not approved for removal while the referencer is uninspectable"),
		PlanValue.RemovableGuids.Contains(Selected));
	TestFalse(TEXT("the patch preview refuses the uninspectable resident referencer before any mutation"), bPreviewed);
	TestEqual(TEXT("the refused patch preview reports INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));

	TestNull(TEXT("the refused preview does not load the referencer Blueprint asset"),
		FindObject<UBlueprint>(nullptr, *ExternalObjectPath));
	UPackage* const ResidentReferencerAfter = FindPackage(nullptr, *ExternalPackageName);
	TestNotNull(TEXT("the referencer package stays resident after the refused preview"), ResidentReferencerAfter);
	TestEqual(TEXT("the refused preview leaves the referencer package dirty state unchanged"),
		ResidentReferencerAfter && ResidentReferencerAfter->IsDirty(), bReferencerDirtyBefore);
	TestEqual(TEXT("the refused preview leaves the target graph unchanged"),
		CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the refused preview leaves the designer widget tree unchanged"),
		CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);
	TestEqual(TEXT("the refused preview leaves the compiled component bindings unchanged"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);
	TestEqual(TEXT("the refused preview leaves the target package dirty state unchanged"),
		Fixture.Package->IsDirty(), bTargetDirtyBefore);
	TestEqual(TEXT("the refused preview runs no editor Blueprint compilation"), Operations.EditorCompiles, 0);
	TestEqual(TEXT("the refused preview requests no target compile"), Operations.TargetCompiles, 0);
	TestEqual(TEXT("the refused preview requests no recovery compile"), Operations.RecoveryCompiles, 0);
	TestEqual(TEXT("the refused preview saves nothing"), Operations.Saves, 0);
	TestTrue(TEXT("the target asset bytes are unchanged"), SameBytes(TargetBytesBefore, ReadBytes(TargetFilename)));
	TestTrue(TEXT("the referencer asset bytes are unchanged"),
		SameBytes(ExternalBytesBefore, ReadBytes(ExternalFilename)));

	External.Cleanup();
	Fixture.Cleanup();
	PurgeReferencerPackage(ExternalPackageName);
	IFileManager::Get().Delete(*TargetFilename, false, true, true);
	IFileManager::Get().Delete(*ExternalFilename, false, true, true);
	return true;
}

// CortexSandbox #112 second review: a referencer package that is not resident when the preview runs
// was auto-loaded so its graphs could be scanned, and UE 5.8 can compile that Blueprint as part of
// the load. A dry run must never add packages to memory to answer the caller question: the
// referencer is uninspectable without loading it, so the preview refuses and leaves it alone.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireNonresidentReferencerTest,
	"Cortex.Graph.Authoring.Migration.Retire.RefusesNonresidentReferencerWithoutLoadingIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireNonresidentReferencerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded custom-event fixture is created"),
		Fixture.Build(TEXT("BP_RetireNonresidentReferencerTarget")));
	if (!Fixture.IsComplete()) { Fixture.Cleanup(); return false; }

	const FString Selected = Fixture.Custom->NodeGuid.ToString();
	const FString TargetFilename = Fixture.Filename();
	IFileManager::Get().Delete(*TargetFilename, false, true, true);
	TestTrue(TEXT("the target asset is saved before the referencer exists"), Fixture.SaveToDisk());

	FExternalCallerFixture External;
	const bool bReferencerBuilt = External.Build(TEXT("BP_RetireNonresidentReferencerHost"),
		Fixture.Blueprint, Fixture.Custom->CustomFunctionName);
	TestTrue(TEXT("a second Widget Blueprint calls the target's generated custom event function"), bReferencerBuilt);
	const FString ExternalFilename = External.Filename();
	const FString ExternalPackageName = External.PackageName();
	const FString ExternalObjectPath = External.ObjectPath();
	if (!bReferencerBuilt)
	{
		External.Cleanup();
		Fixture.Cleanup();
		IFileManager::Get().Delete(*TargetFilename, false, true, true);
		if (!ExternalFilename.IsEmpty()) IFileManager::Get().Delete(*ExternalFilename, false, true, true);
		return false;
	}
	IFileManager::Get().Delete(*ExternalFilename, false, true, true);
	TestTrue(TEXT("the referencer asset is saved"), External.SaveToDisk());
	const TArray<uint8> TargetBytesBefore = ReadBytes(TargetFilename);
	const TArray<uint8> ExternalBytesBefore = ReadBytes(ExternalFilename);

	// The nonresident state: only the referencer's bytes exist, so any inspection would have to load it.
	TestTrue(TEXT("the referencer is fully unloaded, leaving only its bytes on disk"), External.UnloadCompletely());
	TestNull(TEXT("the referencer package is not resident at preview time"), FindPackage(nullptr, *ExternalPackageName));
	TestTrue(TEXT("the referencer package still exists on disk"), FPackageName::DoesPackageExist(ExternalPackageName));

	FString RegistryFailure;
	const bool bRegistryKnowsReferencer = ScanSavedReferencers({ TargetFilename, ExternalFilename },
		Fixture.Package->GetFName(), FName(*ExternalPackageName), RegistryFailure);
	TestTrue(FString::Printf(TEXT("fixture precondition: the Asset Registry reports the saved referencer: %s"),
		*RegistryFailure), bRegistryKnowsReferencer);
	if (!bRegistryKnowsReferencer)
	{
		Fixture.Cleanup();
		IFileManager::Get().Delete(*TargetFilename, false, true, true);
		IFileManager::Get().Delete(*ExternalFilename, false, true, true);
		return false;
	}

	// Only a refused preview can still change any oracle captured below.
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	const bool bTargetDirtyBefore = Fixture.Package->IsDirty();

	FOperations Operations;
	Operations.Begin();
	FCortexCommandResult Error;
	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	const bool bPlanned = Plan(Fixture, { Selected }, PlanValue, bReused, Error);
	FCortexGraphPreparedPatch Preview;
	Error = FCortexCommandResult();
	const bool bPreviewed = PreviewRetirement(Fixture, { Selected },
		TEXT("00000000-0000-0000-0000-000000112202"), Preview, Error);
	Operations.End();

	TestFalse(TEXT("the preview refuses a saved nonresident referencer instead of loading it"), bPlanned);
	TestEqual(TEXT("the nonresident referencer refusal is INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
	TestTrue(TEXT("the nonresident referencer refusal names the referencer and why it could not be inspected"),
		ReferencerRefusalIsDiagnostic(Error, ExternalPackageName));
	TestFalse(TEXT("the selected custom event is not approved for removal while the referencer is not inspectable"),
		PlanValue.RemovableGuids.Contains(Selected));
	TestFalse(TEXT("the patch preview refuses the nonresident referencer before any mutation"), bPreviewed);
	TestEqual(TEXT("the refused patch preview reports INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));

	TestNull(TEXT("the refused preview does not load the referencer package"),
		FindPackage(nullptr, *ExternalPackageName));
	TestNull(TEXT("the refused preview does not load the referencer Blueprint asset"),
		FindObject<UBlueprint>(nullptr, *ExternalObjectPath));
	TestEqual(TEXT("the refused preview leaves the target graph unchanged"),
		CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the refused preview leaves the designer widget tree unchanged"),
		CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);
	TestEqual(TEXT("the refused preview leaves the compiled component bindings unchanged"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);
	TestEqual(TEXT("the refused preview leaves the target package dirty state unchanged"),
		Fixture.Package->IsDirty(), bTargetDirtyBefore);
	TestEqual(TEXT("the refused preview runs no editor Blueprint compilation"), Operations.EditorCompiles, 0);
	TestEqual(TEXT("the refused preview requests no target compile"), Operations.TargetCompiles, 0);
	TestEqual(TEXT("the refused preview requests no recovery compile"), Operations.RecoveryCompiles, 0);
	TestEqual(TEXT("the refused preview saves nothing"), Operations.Saves, 0);
	TestTrue(TEXT("the target asset bytes are unchanged"), SameBytes(TargetBytesBefore, ReadBytes(TargetFilename)));
	TestTrue(TEXT("the referencer asset bytes are unchanged"),
		SameBytes(ExternalBytesBefore, ReadBytes(ExternalFilename)));

	External.Cleanup();
	Fixture.Cleanup();
	PurgeReferencerPackage(ExternalPackageName);
	IFileManager::Get().Delete(*TargetFilename, false, true, true);
	IFileManager::Get().Delete(*ExternalFilename, false, true, true);
	return true;
}

// CortexSandbox #112: a component-bound entry names a component property and a delegate owner, so
// the two must describe the same binding. A property whose compiled class cannot host the named
// delegate is not a live binding at all; the preview must refuse it instead of authorizing removal.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCortexGraphMigrationRetireComponentBoundMismatchTest,
	"Cortex.Graph.Authoring.Migration.Retire.ComponentBoundRefusesIncompatibleDelegateOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRetireComponentBoundMismatchTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRetireTest;
	FGuardedEntryFixture Fixture;
	TestTrue(TEXT("guarded fixture with an unrelated compiled widget variable is created"),
		Fixture.Build(TEXT("BP_RetireComponentBoundMismatch"), true));
	if (!Fixture.IsComplete() || !Fixture.IncompatibleComponentProperty) { Fixture.Cleanup(); return false; }

	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("the fixture asset is saved before the mismatched binding exists"), Fixture.SaveToDisk());
	const TArray<uint8> DiskBefore = ReadBytes(Filename);

	UK2Node_ComponentBoundEvent* Mismatched =
		NewObject<UK2Node_ComponentBoundEvent>(Fixture.Graph, NAME_None, RF_Transactional);
	Mismatched->CreateNewGuid();
	Mismatched->InitializeComponentBoundEventParams(Fixture.IncompatibleComponentProperty, Fixture.ClickedDelegate);
	Fixture.Graph->AddNode(Mismatched, true, false);
	Mismatched->AllocateDefaultPins();
	UK2Node_CallFunction* MismatchedBody = Fixture.AddBody(Mismatched);
	TestFalse(TEXT("the compiled component property class cannot host the bound native delegate"),
		Fixture.IncompatibleComponentProperty->PropertyClass->IsChildOf(UButton::StaticClass()));
	TestTrue(TEXT("the mismatched node names the incompatible component property"),
		Mismatched->ComponentPropertyName == FName(TEXT("IncompatiblePanel")));
	TestTrue(TEXT("the mismatched node still names the native button delegate and its owner"),
		Mismatched->DelegatePropertyName == FName(TEXT("OnClicked"))
			&& Mismatched->DelegateOwnerClass == UButton::StaticClass());
	TestNotNull(TEXT("the mismatched binding owns a genuine downstream body"), MismatchedBody);
	if (!MismatchedBody)
	{
		Fixture.Cleanup();
		IFileManager::Get().Delete(*Filename, false, true, true);
		return false;
	}

	const FString Selected = Mismatched->NodeGuid.ToString();
	const FString GraphBefore = CaptureNativeGraph(Fixture.Graph);
	const FString WidgetsBefore = CaptureWidgetTree(Fixture.Blueprint);
	const FString BindingsBefore = CaptureComponentBindings(Fixture.Blueprint);
	const bool bDirtyBefore = Fixture.Package->IsDirty();

	FCortexGraphMigrationRetirePlan PlanValue;
	bool bReused = false;
	FCortexCommandResult Error;
	TestFalse(TEXT("a component property that cannot host the bound delegate is refused"),
		Plan(Fixture, { Selected }, PlanValue, bReused, Error));
	TestEqual(TEXT("the mismatched component binding refusal is INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
	TestTrue(TEXT("the mismatched component binding refusal is diagnostic"), !Error.ErrorMessage.IsEmpty());

	Error = FCortexCommandResult();
	FCortexGraphPreparedPatch Preview;
	TestFalse(TEXT("the patch preview refuses the mismatched component binding before any mutation"),
		PreviewRetirement(Fixture, { Selected }, TEXT("00000000-0000-0000-0000-000000112102"), Preview, Error));
	TestEqual(TEXT("the refused mismatched preview reports INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));

	TestEqual(TEXT("the refused preview leaves the graph unchanged"),
		CaptureNativeGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the refused preview leaves the designer widget tree unchanged"),
		CaptureWidgetTree(Fixture.Blueprint), WidgetsBefore);
	TestEqual(TEXT("the refused preview leaves the compiled component bindings unchanged"),
		CaptureComponentBindings(Fixture.Blueprint), BindingsBefore);
	TestEqual(TEXT("the refused preview leaves the dirty state unchanged"),
		Fixture.Package->IsDirty(), bDirtyBefore);
	TestNotNull(TEXT("the mismatched binding node is still in the graph"),
		FCortexGraphMigrationOps::FindNodeByGuid(Fixture.Blueprint, Mismatched->NodeGuid));
	TestTrue(TEXT("the staged asset bytes are unchanged"), SameBytes(DiskBefore, ReadBytes(Filename)));

	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

#endif

