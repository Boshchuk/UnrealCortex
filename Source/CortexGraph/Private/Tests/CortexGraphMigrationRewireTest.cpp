/**
 * `migration.op="replace_call_output"` coverage (CortexSandbox issue #113).
 *
 * The fixture is a real Widget Blueprint function graph whose `UK2Node_CallFunction` was rebuilt
 * against a migrated native declaration. The historic `bPassedRequirements` output survives that
 * rebuild as an in-use orphan pin (`bOrphanedPin == true`, `bNotConnectable == true`) that still
 * feeds two independent consumers, while the current `ReturnValue` output exists and the asset is a
 * real compiler error. Every case drives the real native entry points
 * (`FCortexGraphPatchOps::Preflight` / `Execute`) and the published `graph.apply_patch` route, so the
 * envelope, the durable plan, the published edge inventory and the recovery oracle are exercised
 * exactly as a connected editor exercises them. The edge inventory assertion is an independent
 * test-side scan of the orphan's live links, never a mock echo.
 *
 * The fixture builds three signature shapes from real native declarations: the review shape (a
 * historic out parameter that survives as an in-use orphan output), the same-name shape (the historic
 * out parameter became an input of the same name beside the new return value, so one name carries both
 * directions), and a current-declaration call that never had a historic output at all.
 */

#include "Misc/AutomationTest.h"

#include "CortexCommandRouter.h"
#include "CortexGraphCommandHandler.h"
#include "CortexGraphMigrationTestTypes.h"
#include "CortexGraphTestContentRoot.h"
#include "Operations/CortexGraphMigrationOps.h"
#include "Operations/CortexGraphPatchOps.h"
#include "Operations/CortexGraphPatchState.h"

#include "Dom/JsonObject.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "HAL/FileManager.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Copy.h"
#include "K2Node_FunctionEntry.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "WidgetBlueprint.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS
namespace CortexGraphMigrationRewireTest
{
/** The historic output pin the migrated declaration no longer exposes. */
const TCHAR* const StalePinName = TEXT("bPassedRequirements");
/** The current output pin that must take over both reviewed consumers. */
const TCHAR* const ReplacementPinName = TEXT("ReturnValue");
/** Historic output name of the signature shape whose migrated name became an input parameter. */
const TCHAR* const SameNameHistoricPinName = TEXT("bGuestEmailValid");

/** Signature shape one fixture graph is rebuilt from. */
enum class EFixtureSignature : uint8
{
	/** The historic result was an out parameter, so the rebuild leaves an in-use orphan output. */
	LegacyOutParameter,
	/** The historic out parameter became an input of the same name beside the new return value. */
	SameNameInputParameter,
	/** A call node for the current declaration that never carried a historic output at all. */
	CurrentOnly,
};

/**
 * One real Widget Blueprint function graph holding a rebuilt call node.
 *
 * `Build` links the historic output to two independent input consumers, then performs the engine's own
 * signature migration (`SetFromFunction` + `ReconstructNode`), which is exactly what leaves the linked
 * output pin orphaned while `ReturnValue` appears. A real compile afterwards turns the in-use orphan
 * into the compiler error the repair must clear.
 */
struct FFixture
{
	UPackage* Package = nullptr;
	UWidgetBlueprint* Blueprint = nullptr;
	UEdGraph* Graph = nullptr;
	UK2Node_CallFunction* Call = nullptr;
	UK2Node_CallFunction* EnabledConsumer = nullptr;
	UK2Node_CallFunction* BoolToInt = nullptr;
	UK2Node_CallFunction* CountConsumer = nullptr;
	/**
	 * Optional third consumer whose own connection callback rewrites pins beyond the reviewed input.
	 * Only the consumer-with-side-effect test creates it.
	 */
	UK2Node_Copy* CopyConsumer = nullptr;
	/** Historic output pin name of the signature shape this fixture was built for. */
	FName HistoricPinName = NAME_None;

	UK2Node_CallFunction* AddCall(UFunction* Function, const int32 X, const int32 Y)
	{
		if (!Graph || !Function) return nullptr;
		UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph, NAME_None, RF_Transactional);
		Node->SetFromFunction(Function);
		Node->CreateNewGuid();
		Node->AllocateDefaultPins();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Graph->AddNode(Node, true, false);
		return Node;
	}

	/**
	 * Adds a reviewed consumer whose own callback mutates pins beyond the reviewed input: a real
	 * `UK2Node_Copy`, whose wildcard input *and* wildcard output both conform to the reconnected bool.
	 *
	 * The historic output is linked with the raw pin API on purpose: the engine's notification path
	 * would conform the wildcards immediately, and this fixture has to start from the pre-callback
	 * wildcard state so the rewire's own snapshot is the only thing that can restore it.
	 */
	bool AddCopyConsumer()
	{
		if (!Graph || !Call) return false;
		CopyConsumer = NewObject<UK2Node_Copy>(Graph, NAME_None, RF_Transactional);
		if (!CopyConsumer) return false;
		CopyConsumer->CreateNewGuid();
		CopyConsumer->AllocateDefaultPins();
		CopyConsumer->NodePosX = 400;
		CopyConsumer->NodePosY = 360;
		Graph->AddNode(CopyConsumer, true, false);
		UEdGraphPin* const ItemPin = CopyConsumer->FindPin(UEdGraphSchema_K2::PN_Item, EGPD_Input);
		UEdGraphPin* const ResultPin = CopyConsumer->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
		UEdGraphPin* const Historic = FindHistoricOutputPin();
		if (!ItemPin || !ResultPin || !Historic) return false;
		Historic->MakeLinkTo(ItemPin);
		return ItemPin->LinkedTo.Num() == 1
			&& ItemPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
			&& ResultPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard
			&& ResultPin->LinkedTo.IsEmpty();
	}

	/**
	 * Creates the fixture graph for one signature shape.
	 *
	 * `LegacyOutParameter` is the review fixture: the historic out parameter survives the engine's own
	 * signature migration as an in-use orphan output feeding two consumers. `SameNameInputParameter`
	 * rebuilds from the declaration whose historic out parameter became an input of the same name, so
	 * the rebuilt call node carries a surviving input and an in-use orphan output under one name.
	 * `CurrentOnly` links a call node for the current declaration and never creates a historic output,
	 * so an absent-orphan request against it is a mistargeted migration, not a replay.
	 */
	bool Build(const TCHAR* Name, const EFixtureSignature Signature = EFixtureSignature::LegacyOutParameter)
	{
		HistoricPinName = Signature == EFixtureSignature::SameNameInputParameter
			? FName(SameNameHistoricPinName) : FName(StalePinName);
		EnsureCortexGraphTestTempContentRoot();
		Package = CreatePackage(*FString::Printf(TEXT("/Game/Temp/%s"), Name));
		Blueprint = Cast<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UCortexGraphRewireFixtureWidget::StaticClass(), Package, FName(Name), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		if (!Blueprint) return false;

		// A Widget Blueprint compile dereferences its widget tree, so the fixture owns a minimal real
		// designer hierarchy. It is unrelated to the repair and must survive it unchanged.
		UWidgetTree* Tree = Blueprint->WidgetTree;
		if (!Tree)
		{
			Tree = NewObject<UWidgetTree>(Blueprint, UWidgetTree::StaticClass(), TEXT("WidgetTree"));
			Blueprint->WidgetTree = Tree;
		}
		UCanvasPanel* Root = Tree ? Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root")) : nullptr;
		if (!Root) return false;
		Tree->RootWidget = Root;

		Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, FName(TEXT("RewireGate")),
			UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (!Graph) return false;
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, true, nullptr);

		const bool bSameNameSignature = Signature == EFixtureSignature::SameNameInputParameter;
		UClass* const FixtureClass = UCortexGraphRewireFixtureWidget::StaticClass();
		UFunction* const Legacy = FixtureClass->FindFunctionByName(
			bSameNameSignature ? TEXT("LegacyGuestEmailRequirements") : TEXT("LegacyQuestRequirements"));
		UFunction* const Current = FixtureClass->FindFunctionByName(
			bSameNameSignature ? TEXT("UpdateGuestEmailRequirements") : TEXT("UpdateFromQuestRequirements"));
		UFunction* const Gate = FixtureClass->FindFunctionByName(TEXT("SetGateEnabled"));
		UFunction* const Count = FixtureClass->FindFunctionByName(TEXT("SetGateCount"));
		UFunction* const Convert = UKismetMathLibrary::StaticClass()->FindFunctionByName(TEXT("Conv_BoolToInt"));
		if (!Legacy || !Current || !Gate || !Count || !Convert) return false;

		Call = AddCall(Signature == EFixtureSignature::CurrentOnly ? Current : Legacy, 0, 0);
		EnabledConsumer = AddCall(Gate, 400, -120);
		BoolToInt = AddCall(Convert, 400, 120);
		CountConsumer = AddCall(Count, 800, 120);
		if (!Call || !EnabledConsumer || !BoolToInt || !CountConsumer) return false;

		const UEdGraphSchema* const Schema = Graph->GetSchema();
		auto Link = [Schema](UEdGraphPin* From, UEdGraphPin* To)
		{
			return Schema && From && To && Schema->TryCreateConnection(From, To);
		};

		UEdGraphPin* const Then = Call->FindPin(TEXT("then"));
		UEdGraphPin* const ConvertOut = BoolToInt->FindPin(FName(ReplacementPinName));
		UEdGraphPin* const CountIn = CountConsumer->FindPin(TEXT("Count"));
		if (!Then || !ConvertOut || !CountIn) return false;
		if (!Link(ConvertOut, CountIn)) return false;

		// Two independent consumers keep their links to the historic output: the direct bool input of
		// the gate consumer and the bool input of the engine's bool-to-int conversion, whose int
		// result then feeds the second consumer. A current-declaration call has no historic output, so
		// both consumer inputs stay unconnected there.
		if (Signature != EFixtureSignature::CurrentOnly)
		{
			UEdGraphPin* const Historic = Call->FindPin(HistoricPinName);
			UEdGraphPin* const GateIn = EnabledConsumer->FindPin(TEXT("bEnabled"));
			UEdGraphPin* const ConvertIn = BoolToInt->FindPin(TEXT("InBool"));
			if (!Historic || !GateIn || !ConvertIn) return false;
			if (!Link(Historic, GateIn) || !Link(Historic, ConvertIn)) return false;
		}

		if (!Link(Then, EnabledConsumer->FindPin(TEXT("execute")))) return false;
		// A K2 exec output accepts exactly one connection: a second TryCreateConnection on the call's
		// `then` would replace the first link instead of fanning out, so the second consumer is chained
		// after the first one, keeping both on the entry's exec chain.
		if (!Link(EnabledConsumer->FindPin(TEXT("then")), CountConsumer->FindPin(TEXT("execute")))) return false;

		// The function graph's entry must drive the call through an exec wire. The compiler prunes
		// impure nodes that are unreachable via exec wires (`ExpansionStep` -> `PruneIsolatedNodes`)
		// *before* it validates the graph, and a pruned node is never validated, so a dangling call
		// would have its in-use orphan pin silently dropped instead of reported as a compile error.
		UK2Node_FunctionEntry* Entry = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_FunctionEntry* Candidate = Cast<UK2Node_FunctionEntry>(Node))
			{
				Entry = Candidate;
				break;
			}
		}
		if (!Entry) return false;
		if (!Link(Entry->FindPin(TEXT("then")), Call->FindPin(TEXT("execute")))) return false;

		if (Signature == EFixtureSignature::CurrentOnly)
		{
			// No historic declaration was ever called, so the only compile is the valid one: a
			// current-declaration call with no in-use orphan must compile cleanly. That is the
			// fixture's proof that it carries no unrelated compiler error.
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			return IsCurrentCallIntact();
		}

		// The engine's own migration: rebuild the live call against the current declaration. The linked
		// historic output is retained as an orphan pin because `UK2Node_CallFunction` saves all
		// orphaned pins and the old pin is still in use.
		Call->SetFromFunction(Current);
		Call->ReconstructNode();

		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return IsOrphanStateIntact();
	}

	/** On-disk filename of the fixture package. */
	FString Filename() const
	{
		return Package ? FPackageName::LongPackageNameToFilename(Package->GetName(),
			FPackageName::GetAssetPackageExtension()) : FString();
	}

	/** Baseline persistence, so disk-byte oracles compare real files instead of assumptions. */
	bool SaveToDisk()
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		return Package && Blueprint && UPackage::SavePackage(Package, Blueprint, *Filename(), SaveArgs);
	}

	UEdGraphPin* FindCallPin(const FName PinName) const
	{
		return Call ? Call->FindPin(PinName) : nullptr;
	}

	/**
	 * The fixture's historic pin resolved by direction as well as name. The same-name signature shape
	 * carries a surviving input and an orphan output under one name, so a name-only lookup would
	 * silently return the wrong one.
	 */
	UEdGraphPin* FindHistoricOutputPin() const
	{
		return Call ? Call->FindPin(HistoricPinName, EGPD_Output) : nullptr;
	}

	/**
	 * The exec chain that keeps every impure node reachable: entry.then -> call.execute, then
	 * call.then -> first consumer -> second consumer. A K2 exec output takes exactly one connection, so
	 * the two consumers are chained rather than fanned out from the call.
	 */
	bool IsExecChainIntact() const
	{
		UEdGraphPin* const Execute = FindCallPin(TEXT("execute"));
		UEdGraphPin* const Then = FindCallPin(TEXT("then"));
		if (!Execute || Execute->LinkedTo.Num() != 1 || !Then || Then->LinkedTo.Num() != 1) return false;
		const UEdGraphNode* const FirstConsumer = Then->LinkedTo[0] ? Then->LinkedTo[0]->GetOwningNode() : nullptr;
		UEdGraphPin* const FirstThen = FirstConsumer ? FirstConsumer->FindPin(TEXT("then")) : nullptr;
		return FirstThen && FirstThen->LinkedTo.Num() == 1;
	}

	/** Independent native oracle: the orphan pin, its flags, its two links and the valid output. */
	bool IsOrphanStateIntact() const
	{
		UEdGraphPin* const Stale = FindHistoricOutputPin();
		UEdGraphPin* const Replacement = FindCallPin(FName(ReplacementPinName));
		// The call must stay on the entry's exec chain, otherwise the compiler prunes the unreachable
		// impure nodes before validating them and the fixture would no longer be compiler-invalid.
		return Stale && Stale->bOrphanedPin && Stale->bNotConnectable && Stale->LinkedTo.Num() == 2
			&& Replacement && !Replacement->bOrphanedPin && Replacement->LinkedTo.Num() == 0
			&& IsExecChainIntact()
			&& Blueprint && Blueprint->Status == BS_Error;
	}

	/**
	 * Independent native oracle of the current-declaration fixture: the call carries the current
	 * return output, never carried the historic output, and the whole asset compiles clean. A clean
	 * status here is the fixture's proof that it holds no unrelated compiler error.
	 */
	bool IsCurrentCallIntact() const
	{
		UEdGraphPin* const Replacement = FindCallPin(FName(ReplacementPinName));
		return Call && !FindHistoricOutputPin()
			&& Replacement && !Replacement->bOrphanedPin && Replacement->LinkedTo.Num() == 0
			&& IsExecChainIntact()
			&& Blueprint && Blueprint->Status == BS_UpToDate;
	}

	/** Canonical `"<far_node_guid>.<far_pin>"` endpoints of the orphan's live links. */
	TArray<FString> OrphanEdgeEndpoints() const
	{
		TArray<FString> Endpoints;
		UEdGraphPin* const Stale = FindHistoricOutputPin();
		if (!Stale) return Endpoints;
		for (const UEdGraphPin* LinkedPin : Stale->LinkedTo)
		{
			const UEdGraphNode* const FarNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			if (!FarNode || !LinkedPin) continue;
			Endpoints.Add(FarNode->NodeGuid.ToString() + TEXT(".") + LinkedPin->PinName.ToString());
		}
		Endpoints.Sort();
		return Endpoints;
	}

	void Cleanup()
	{
		if (Blueprint) { Blueprint->ClearFlags(RF_Standalone); Blueprint->MarkAsGarbage(); Blueprint = nullptr; }
		if (Package) { Package->ClearFlags(RF_Standalone); Package->MarkAsGarbage(); Package = nullptr; }
		Graph = nullptr;
	}
};

/** Independent native graph snapshot, including the orphan/connectability flags of every pin. */
FString CaptureRewireGraph(UEdGraph* Graph)
{
	TArray<FString> Records;
	if (!Graph) return FString();
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		const FString Guid = Node->NodeGuid.ToString();
		Records.Add(FString::Printf(TEXT("N|%s|%s|%d|%d"), *Guid, *Node->GetClass()->GetPathName(),
			Node->NodePosX, Node->NodePosY));
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			Records.Add(FString::Printf(TEXT("P|%s|%s|%d|%s|%d|%d"), *Guid, *Pin->PinName.ToString(),
				static_cast<int32>(Pin->Direction), *Pin->PinType.PinCategory.ToString(),
				Pin->bOrphanedPin ? 1 : 0, Pin->bNotConnectable ? 1 : 0));
			for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				const UEdGraphNode* FarNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
				if (!FarNode || !LinkedPin) continue;
				const FString From = Guid + TEXT(".") + Pin->PinName.ToString();
				const FString To = FarNode->NodeGuid.ToString() + TEXT(".") + LinkedPin->PinName.ToString();
				if (From < To) Records.Add(TEXT("L|") + From + TEXT("|") + To);
			}
		}
	}
	Records.Sort();
	return FString::Join(Records, TEXT("\n"));
}

FString LiveGraphHash(UBlueprint* Blueprint)
{
	FString Hash;
	const TSharedPtr<FJsonObject> Fingerprint = FCortexGraphPatchState::ComputeFingerprint(Blueprint);
	if (Fingerprint.IsValid()) Fingerprint->TryGetStringField(TEXT("graph_authoring_hash"), Hash);
	return Hash;
}

/**
 * Independent native pin-state capture of one node: every pin's name, direction, canonical category,
 * default value, orphan/connectability flags and durable links, including the far endpoint of each
 * link. A rewire apply is required to snapshot each reviewed consumer before its first changed link,
 * so a rollback is proved against this capture instead of against the call node alone.
 */
FString CaptureNodePins(const UEdGraphNode* Node)
{
	TArray<FString> Records;
	if (!Node) return FString();
	const FString Guid = Node->NodeGuid.ToString();
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin) continue;
		Records.Add(FString::Printf(TEXT("P|%s|%s|%d|%s|%s|%d|%d"), *Guid, *Pin->PinName.ToString(),
			static_cast<int32>(Pin->Direction), *Pin->PinType.PinCategory.ToString(), *Pin->DefaultValue,
			Pin->bOrphanedPin ? 1 : 0, Pin->bNotConnectable ? 1 : 0));
		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			const UEdGraphNode* FarNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
			if (!FarNode || !LinkedPin) continue;
			Records.Add(TEXT("L|") + Guid + TEXT(".") + Pin->PinName.ToString() + TEXT("|")
				+ FarNode->NodeGuid.ToString() + TEXT(".") + LinkedPin->PinName.ToString());
		}
	}
	Records.Sort();
	return FString::Join(Records, TEXT("\n"));
}

/**
 * Every reviewed consumer of the orphan output, as one comparable native pin-state capture. The
 * optional side-effect consumer is appended too, so a rollback proves its own callback's pin changes
 * were reverted instead of only the call node.
 */
FString ConsumerPinState(const FFixture& Fixture)
{
	return CaptureNodePins(Fixture.EnabledConsumer) + TEXT("\n--\n")
		+ CaptureNodePins(Fixture.BoolToInt) + TEXT("\n--\n")
		+ CaptureNodePins(Fixture.CountConsumer) + TEXT("\n--\n")
		+ CaptureNodePins(Fixture.CopyConsumer);
}

/** One side-effect consumer pin's live canonical category, read straight from the native pin. */
FString CopyPinCategory(const FFixture& Fixture, const FName PinName, const EEdGraphPinDirection Direction)
{
	UEdGraphPin* const Pin = Fixture.CopyConsumer ? Fixture.CopyConsumer->FindPin(PinName, Direction) : nullptr;
	return Pin ? Pin->PinType.PinCategory.ToString() : FString(TEXT("<missing>"));
}

UEdGraphNode* FindNodeInGraph(UEdGraph* Graph, const FGuid& Guid)
{
	if (!Graph) return nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && Node->NodeGuid == Guid) return Node;
	}
	return nullptr;
}

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

/**
 * Reads one published edge inventory into canonical `"<far_node_guid>.<far_pin>"` endpoints.
 *
 * Both the compact published inventory (`edges` at the result root) and the durable normalized
 * migration plan (`migration.edges`) are accepted, and each entry may be a plain key string or a
 * `{far_node_guid, far_pin}` object, so the assertion pins the exact edge set instead of one
 * presentation detail.
 */
bool ExtractEdgeEndpoints(const TSharedPtr<FJsonObject>& Object, TArray<FString>& OutEndpoints)
{
	OutEndpoints.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Edges = nullptr;
	if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("edges"), Edges) || !Edges) return false;
	for (const TSharedPtr<FJsonValue>& Value : *Edges)
	{
		if (!Value.IsValid()) return false;
		if (Value->Type == EJson::String)
		{
			OutEndpoints.Add(Value->AsString());
			continue;
		}
		const TSharedPtr<FJsonObject> Edge = Value->AsObject();
		FString FarGuid;
		FString FarPin;
		if (!Edge.IsValid() || !Edge->TryGetStringField(TEXT("far_node_guid"), FarGuid)
			|| !Edge->TryGetStringField(TEXT("far_pin"), FarPin)) return false;
		OutEndpoints.Add(FarGuid + TEXT(".") + FarPin);
	}
	OutEndpoints.Sort();
	return true;
}

bool PublishedEdgeEndpoints(const TSharedPtr<FJsonObject>& Object, TArray<FString>& OutEndpoints)
{
	OutEndpoints.Reset();
	if (!Object.IsValid()) return false;
	const TSharedPtr<FJsonObject>* Migration = nullptr;
	const TSharedPtr<FJsonObject> Scope =
		(Object->TryGetObjectField(TEXT("migration"), Migration) && Migration && Migration->IsValid())
			? *Migration : Object;
	return ExtractEdgeEndpoints(Scope, OutEndpoints);
}

/**
 * Builds the published `graph.apply_patch` request for `migration.op="replace_call_output"` with the
 * exact graph GUID, call node GUID, pin names and consumer identities.
 *
 * `DeclaredEdges` is the caller-reviewed mapping (`"<far_node_guid>.<far_pin>"` keys); a preview
 * omits it because the discovered inventory is published instead, while an apply must carry it.
 */
TSharedPtr<FJsonObject> RewireRequest(
	const FFixture& Fixture,
	const TCHAR* PatchId,
	const TArray<FString>* DeclaredEdges = nullptr,
	const FString& ValidationHash = FString(),
	const bool bDryRun = true,
	const bool bCompile = true,
	const bool bSave = false)
{
	if (!Fixture.Blueprint || !Fixture.Graph || !Fixture.Call) return nullptr;
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("asset_path"), Fixture.Blueprint->GetPathName());
	Request->SetStringField(TEXT("patch_id"), PatchId);
	Request->SetObjectField(TEXT("expected_fingerprint"), FCortexGraphPatchState::ComputeFingerprint(Fixture.Blueprint));
	Request->SetArrayField(TEXT("nodes"), {});
	Request->SetArrayField(TEXT("connections"), {});
	Request->SetArrayField(TEXT("pin_updates"), {});
	Request->SetBoolField(TEXT("dry_run"), bDryRun);
	Request->SetBoolField(TEXT("compile"), bCompile);
	Request->SetBoolField(TEXT("save"), bSave);
	Request->SetBoolField(TEXT("allow_noop"), false);

	TSharedPtr<FJsonObject> Migration = MakeShared<FJsonObject>();
	Migration->SetStringField(TEXT("op"), TEXT("replace_call_output"));
	TSharedPtr<FJsonObject> Source = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> GraphRef = MakeShared<FJsonObject>();
	GraphRef->SetStringField(TEXT("graph_guid"), Fixture.Graph->GraphGuid.ToString());
	GraphRef->SetStringField(TEXT("graph_kind"), TEXT("function"));
	Source->SetObjectField(TEXT("graph_ref"), GraphRef);
	Source->SetStringField(TEXT("call_node_guid"), Fixture.Call->NodeGuid.ToString());
	Source->SetStringField(TEXT("stale_pin"), Fixture.HistoricPinName.ToString());
	Source->SetStringField(TEXT("replacement_pin"), ReplacementPinName);
	Migration->SetObjectField(TEXT("source"), Source);

	if (DeclaredEdges)
	{
		TArray<TSharedPtr<FJsonValue>> Edges;
		for (const FString& Key : *DeclaredEdges)
		{
			FString FarGuid;
			FString FarPin;
			if (!Key.Split(TEXT("."), &FarGuid, &FarPin)) return nullptr;
			TSharedPtr<FJsonObject> Edge = MakeShared<FJsonObject>();
			Edge->SetStringField(TEXT("far_node_guid"), FarGuid);
			Edge->SetStringField(TEXT("far_pin"), FarPin);
			Edges.Add(MakeShared<FJsonValueObject>(Edge));
		}
		Migration->SetArrayField(TEXT("edges"), Edges);
	}
	Request->SetObjectField(TEXT("migration"), Migration);
	if (!bDryRun) Request->SetStringField(TEXT("expected_validation_hash"), ValidationHash);
	return Request;
}

/**
 * Rewrites the two pin identities of a built request, for refusal cases that select another pin than
 * the fixture's own historic output. The object shape is the same one the apply publishes, so the
 * refusal is proved against the real request contract instead of a hand-built envelope.
 */
bool SetRequestSourcePins(const TSharedPtr<FJsonObject>& Request, const TCHAR* StalePin, const TCHAR* ReplacementPin)
{
	const TSharedPtr<FJsonObject>* Migration = nullptr;
	const TSharedPtr<FJsonObject>* Source = nullptr;
	if (!Request.IsValid() || !Request->TryGetObjectField(TEXT("migration"), Migration) || !Migration || !Migration->IsValid()
		|| !(*Migration)->TryGetObjectField(TEXT("source"), Source) || !Source || !Source->IsValid())
	{
		return false;
	}
	(*Source)->SetStringField(TEXT("stale_pin"), StalePin);
	(*Source)->SetStringField(TEXT("replacement_pin"), ReplacementPin);
	return true;
}

/** Shared fault/seam reset, so one case can never inherit another's injection. */
void ResetSeams()
{
	FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);
	FCortexGraphPatchOps::SetReadbackFaultForTesting(NAME_None);
	FCortexGraphPatchOps::ClearPreReadbackMutatorForTesting();
	FCortexGraphPatchOps::SetSaveFaultForTesting(false);
	FCortexGraphPatchOps::SetPostSaveVerificationFaultForTesting(NAME_None);
	FCortexGraphMigrationOps::ClearCallOutputReadbackFaultForTesting();
}

/**
 * The fixture deliberately compiles a call whose historic output is still in use, so the engine's own
 * "In use pin ... no longer exists" compile error is the fixture's expected compiler-invalid state and
 * never an unexpected test failure. `Occurrences` follows the automation contract: it is an exact
 * count when positive, and "must be seen one or more times, no upper limit" when zero — either way a
 * fixture that stopped producing the diagnostic fails instead of silently passing.
 *
 * The single-compile tests require exactly one occurrence. The fault table compiles the fixture once
 * per fault case and the repair itself compiles again during apply/recovery, so it requires at least
 * one occurrence.
 */
void ExpectFixtureCompileError(FAutomationTestBase& Test, const int32 Occurrences = 1)
{
	Test.AddExpectedError(TEXT("In use pin"), EAutomationExpectedErrorFlags::Contains, Occurrences);
}

/**
 * Two-phase approved apply: the preview of the exact reviewed intent publishes the validation token,
 * and the apply then carries the same caller-declared edge set the test computed independently from
 * the orphan's live links. The token therefore always describes the intent being applied.
 */
bool PrepareRewireApply(
	FFixture& Fixture,
	const TCHAR* PatchId,
	const TArray<FString>& Edges,
	TSharedPtr<FJsonObject>& OutRequest,
	FCortexCommandResult& OutError,
	const bool bCompile = true,
	const bool bSave = false)
{
	TSharedPtr<FJsonObject> PreviewRequest = RewireRequest(Fixture, PatchId, &Edges);
	FCortexGraphPreparedPatch Preview;
	if (!PreviewRequest.IsValid()
		|| !FCortexGraphPatchOps::Preflight(Fixture.Blueprint, PreviewRequest, Preview, OutError))
	{
		return false;
	}
	OutRequest = RewireRequest(Fixture, PatchId, &Edges, Preview.ValidationHash, false, bCompile, bSave);
	return OutRequest.IsValid();
}

/**
 * Independent observer of the coordinator's real operations and of real disk commits: the target
 * compile counter comes from the patch seam and the save counter from the engine's own
 * `PackageSavedWithContextEvent`, so a claim like "one explicit save" is proved by the engine.
 */
struct FOperations
{
	int32 TargetCompiles = 0;
	int32 Saves = 0;

	void Begin()
	{
		Active = this;
		TargetCompiles = 0;
		Saves = 0;
		FCortexGraphPatchOps::SetOperationObserverForTesting([](const FName Operation, UBlueprint*)
		{
			if (Active && Operation == TEXT("target_compile")) ++Active->TargetCompiles;
		});
		SaveHandle = UPackage::PackageSavedWithContextEvent.AddLambda(
			[](const FString&, UPackage*, FObjectPostSaveContext)
			{
				if (Active) ++Active->Saves;
			});
	}

	void End()
	{
		UPackage::PackageSavedWithContextEvent.Remove(SaveHandle);
		FCortexGraphPatchOps::ClearOperationObserverForTesting();
		Active = nullptr;
	}

private:
	static FOperations* Active;
	FDelegateHandle SaveHandle;
};

FOperations* FOperations::Active = nullptr;

/**
 * Adds `Count` extra reviewed consumers of the fixture's in-use orphan output.
 *
 * Each one is a real `UK2Node_CallFunction` for `SetGateEnabled` whose bool input is linked from the
 * orphan with the raw pin API, exactly like the fixture's own two consumers: the engine's notification
 * path would conform pins this fixture must leave alone, and the planner reads the live links either
 * way. The added consumers exist to grow the published edge inventory to a realistic scale — hundreds
 * of consumers of one orphan output — without fabricating an envelope the planner never sees. The
 * number of consumers really linked is returned, so a caller can prove the fixture reached the scale it
 * claims instead of trusting the loop count.
 */
int32 AddExtraOrphanConsumers(FFixture& Fixture, const int32 Count)
{
	UFunction* const Gate = UCortexGraphRewireFixtureWidget::StaticClass()->FindFunctionByName(TEXT("SetGateEnabled"));
	UEdGraphPin* const Orphan = Fixture.FindHistoricOutputPin();
	if (!Gate || !Orphan) return 0;
	int32 Added = 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UK2Node_CallFunction* const Consumer =
			Fixture.AddCall(Gate, 1200 + (Index % 40) * 240, 400 + (Index / 40) * 80);
		UEdGraphPin* const ConsumerIn = Consumer ? Consumer->FindPin(TEXT("bEnabled"), EGPD_Input) : nullptr;
		if (!ConsumerIn || ConsumerIn->LinkedTo.Num() != 0) continue;
		Orphan->MakeLinkTo(ConsumerIn);
		if (Orphan->LinkedTo.Contains(ConsumerIn)) ++Added;
	}
	return Added;
}

/**
 * Independent native capture of one pin: identity, direction, canonical type, default, orphan and
 * connectability flags, and its sorted durable links. Used to prove a same-named surviving input is
 * untouched by a repair that removes the orphan output carrying its name.
 */
FString CapturePinRecord(const UEdGraphPin* Pin)
{
	if (!Pin) return FString(TEXT("<missing>"));
	TArray<FString> Links;
	for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		const UEdGraphNode* const FarNode = LinkedPin ? LinkedPin->GetOwningNode() : nullptr;
		Links.Add(FarNode && LinkedPin
			? FarNode->NodeGuid.ToString() + TEXT(".") + LinkedPin->PinName.ToString()
			: FString(TEXT("<dangling>")));
	}
	Links.Sort();
	return FString::Printf(TEXT("%s|id=%s|dir=%d|cat=%s|sub=%s|def=%s|orphan=%d|nc=%d|links=%s"),
		*Pin->PinName.ToString(), *Pin->PinId.ToString(), static_cast<int32>(Pin->Direction),
		*Pin->PinType.PinCategory.ToString(), *Pin->PinType.PinSubCategory.ToString(), *Pin->DefaultValue,
		Pin->bOrphanedPin ? 1 : 0, Pin->bNotConnectable ? 1 : 0, *FString::Join(Links, TEXT(",")));
}
}

// ---------------------------------------------------------------------------
// 1. The orphan output previews side-effect-free and publishes exactly its two live edges
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireOrphanOutputPreviewTest,
	"Cortex.Graph.Authoring.Migration.Rewire.OrphanOutputPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireOrphanOutputPreviewTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewire_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// Fixture fidelity: a real in-use orphan output with two consumers, a real current output and a
	// real compiler error. These are the test-side oracle the repair must preserve, not a mock.
	UEdGraphPin* const Stale = Fixture.FindCallPin(StalePinName);
	UEdGraphPin* const Replacement = Fixture.FindCallPin(ReplacementPinName);
	TestNotNull(TEXT("the historic output pin exists"), Stale);
	TestTrue(TEXT("the historic output pin is orphaned"), Stale && Stale->bOrphanedPin);
	TestTrue(TEXT("the historic output pin is not connectable"), Stale && Stale->bNotConnectable);
	TestEqual(TEXT("the orphan output keeps its two independent consumers"),
		Stale ? Stale->LinkedTo.Num() : 0, 2);
	TestNotNull(TEXT("the current output pin exists"), Replacement);
	TestTrue(TEXT("the current output pin is a live output"), Replacement && !Replacement->bOrphanedPin);
	// Reachability is why the orphan is actually reported: the compiler validates only nodes it keeps
	// on the entry's exec chain (unreachable impure nodes are pruned before validation). A K2 exec
	// output takes exactly one connection, so the two consumers are chained.
	UEdGraphPin* const ExecuteIn = Fixture.FindCallPin(TEXT("execute"));
	UEdGraphPin* const ThenOut = Fixture.FindCallPin(TEXT("then"));
	TestEqual(TEXT("the call node is driven from the function entry"),
		ExecuteIn ? ExecuteIn->LinkedTo.Num() : 0, 1);
	TestEqual(TEXT("the call node starts the consumer exec chain"),
		ThenOut ? ThenOut->LinkedTo.Num() : 0, 1);
	TestTrue(TEXT("both consumers stay on the entry's exec chain"), Fixture.IsExecChainIntact());
	TestEqual(TEXT("the fixture asset is a real compiler error"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle finds exactly two consumer edges"), ExpectedEdges.Num(), 2);

	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const int32 NodesBefore = Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0;
	const bool bDirtyBefore = Fixture.Package->IsDirty();

	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113001"));
	FCortexGraphPreparedPatch Preview;
	FCortexCommandResult Error;
	const bool bPreviewed = Request.IsValid()
		&& FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Preview, Error);

	TestTrue(FString::Printf(TEXT("replace_call_output preview succeeds [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bPreviewed);

	// A preview is a non-mutating, non-persisting read: fingerprint, the whole native graph (including
	// pin flags and links), the node count and the dirty baseline are all untouched.
	TestEqual(TEXT("preview leaves the fingerprint"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("preview leaves every node, pin, flag and link"), CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("preview leaves the node count"),
		Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0, NodesBefore);
	TestEqual(TEXT("preview leaves the dirty baseline"), Fixture.Package->IsDirty(), bDirtyBefore);

	if (bPreviewed)
	{
		// The preview publishes the discovered inventory, and it must be exactly the orphan's two live
		// consumer edges: no missing consumer, no invented one.
		TArray<FString> PublishedEdges;
		TestTrue(TEXT("preview publishes the discovered edge inventory"),
			PublishedEdgeEndpoints(Preview.NormalizedRequest, PublishedEdges));
		TestEqual(TEXT("preview publishes exactly the orphan's two live edges"),
			FString::Join(PublishedEdges, TEXT("\n")), FString::Join(ExpectedEdges, TEXT("\n")));
	}

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 2. The published graph.apply_patch route previews the same inventory for the same request
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewirePublicRoutePreviewTest,
	"Cortex.Graph.Authoring.Migration.Rewire.PublicRoutePreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewirePublicRoutePreviewTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireRoute_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);

	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"), MakeShared<FCortexGraphCommandHandler>());
	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113002"));
	const FCortexCommandResult Result = Router.Execute(TEXT("graph.apply_patch"), Request);

	TestTrue(FString::Printf(TEXT("graph.apply_patch preview succeeds [%s: %s]"),
		*Result.ErrorCode, *Result.ErrorMessage), Result.bSuccess);

	if (Result.bSuccess && Result.Data.IsValid())
	{
		TestTrue(TEXT("the published preview carries the apply token"),
			Result.Data->HasField(TEXT("validation_hash")));
		TArray<FString> PublishedEdges;
		TestTrue(TEXT("the published preview carries the edge inventory"),
			PublishedEdgeEndpoints(Result.Data, PublishedEdges));
		TestEqual(TEXT("the published inventory is exactly the orphan's two live edges"),
			FString::Join(PublishedEdges, TEXT("\n")), FString::Join(ExpectedEdges, TEXT("\n")));
	}

	TestEqual(TEXT("the published preview mutates nothing"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("the published preview leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 3. An injected apply failure restores the original links *and* the orphan pin flags
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireFailureAtomicityTest,
	"Cortex.Graph.Authoring.Migration.Rewire.FailureAtomicity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireFailureAtomicityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	// The fault table builds the fixture per case and the repair compiles again during apply and
	// recovery, so require at least one occurrence rather than an exact count.
	ExpectFixtureCompileError(*this, 0);

	// The fault seams the repair design names: after the first reconnected edge and after the orphan
	// pin removal. Recovery must restore the orphan pin with its flags and both original links.
	const TCHAR* const Faults[] = {
		TEXT("migration_rewire_after_first_reconnect"),
		TEXT("migration_rewire_after_removal")
	};

	bool bAllPassed = true;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Faults); ++Index)
	{
		FFixture Fixture;
		const FString AssetName = FString::Printf(TEXT("WBP_MigrationRewireAtomic_%d"), Index);
		bAllPassed &= TestTrue(TEXT("rewire fault fixture is created"), Fixture.Build(*AssetName));
		if (!Fixture.Blueprint) { Fixture.Cleanup(); continue; }

		const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
		const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
		const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
		const FString ConsumersBefore = ConsumerPinState(Fixture);
		const bool bDirtyBefore = Fixture.Package->IsDirty();
		// The orphan's original pin GUID: `FindPinById` and `FEdGraphPinReference` resolve by it, so a
		// rollback that recreates the pin with a fresh identity reports `restored` while every existing
		// pin-ID reference to the original orphan stays broken.
		UEdGraphPin* const StaleBefore = Fixture.FindHistoricOutputPin();
		const FGuid OrphanPinIdBefore = StaleBefore ? StaleBefore->PinId : FGuid();
		bAllPassed &= TestTrue(FString::Printf(TEXT("%s: the fixture orphan pin has a valid identity"), Faults[Index]),
			OrphanPinIdBefore.IsValid());

		TSharedPtr<FJsonObject> Request;
		FCortexCommandResult Error;
		const FString PatchId = FString::Printf(TEXT("00000000-0000-0000-0000-00000011301%d"), Index);
		const bool bApproved = PrepareRewireApply(Fixture, *PatchId, ExpectedEdges, Request, Error);
		bAllPassed &= TestTrue(FString::Printf(
			TEXT("%s: the approved apply request prepares [%s: %s]"), Faults[Index], *Error.ErrorCode,
			*Error.ErrorMessage), bApproved);
		if (!bApproved)
		{
			Fixture.Cleanup();
			continue;
		}

		FCortexGraphPatchOps::SetApplyFaultPointForTesting(FName(Faults[Index]));
		FCortexGraphPatchOutcome Outcome;
		Error = FCortexCommandResult();
		bAllPassed &= TestFalse(FString::Printf(TEXT("%s: the injected fault fails the apply"), Faults[Index]),
			FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
		FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);

		bAllPassed &= TestEqual(FString::Printf(TEXT("%s: the transaction is restored"), Faults[Index]),
			Outcome.RollbackStatus, FString(TEXT("restored")));
		bAllPassed &= TestFalse(FString::Printf(TEXT("%s: the asset is not blocked"), Faults[Index]),
			Outcome.bBlocked);

		// The fingerprint the journal compares cannot see the orphan flags, so the recovery oracle is an
		// independent native read of the pin, its identity, both original links and the whole graph.
		UEdGraphPin* const Stale = Fixture.FindHistoricOutputPin();
		bAllPassed &= TestNotNull(FString::Printf(TEXT("%s: the orphan pin is restored"), Faults[Index]), Stale);
		bAllPassed &= TestTrue(FString::Printf(TEXT("%s: the orphan flag is restored"), Faults[Index]),
			Stale && Stale->bOrphanedPin);
		bAllPassed &= TestTrue(FString::Printf(TEXT("%s: the not-connectable flag is restored"), Faults[Index]),
			Stale && Stale->bNotConnectable);
		bAllPassed &= TestTrue(FString::Printf(
			TEXT("%s: the restored orphan pin keeps its original PinId '%s'"), Faults[Index],
			*OrphanPinIdBefore.ToString()), Stale && Stale->PinId == OrphanPinIdBefore);
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s: both original consumer links are restored"), Faults[Index]),
			Stale ? FString::Join(Fixture.OrphanEdgeEndpoints(), TEXT("\n")) : FString(),
			FString::Join(ExpectedEdges, TEXT("\n")));
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s: the reviewed consumers' pin state is restored"), Faults[Index]),
			ConsumerPinState(Fixture), ConsumersBefore);
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s: every node, pin, flag and link is restored"), Faults[Index]),
			CaptureRewireGraph(Fixture.Graph), GraphBefore);
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s: the fingerprint is restored"), Faults[Index]),
			LiveGraphHash(Fixture.Blueprint), HashBefore);
		bAllPassed &= TestEqual(FString::Printf(TEXT("%s: the dirty baseline is restored"), Faults[Index]),
			Fixture.Package->IsDirty(), bDirtyBefore);

		Fixture.Cleanup();
	}
	FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);
	return bAllPassed;
}

// ---------------------------------------------------------------------------
// 4. Only the exact reviewed consumer set is accepted: a missing or an extra edge refuses
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireUnreviewedEdgesRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.UnreviewedEdgeInventoryRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireUnreviewedEdgesRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireEdges_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const int32 NodesBefore = Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0;
	TestEqual(TEXT("the fixture oracle finds the two reviewed consumers"), ExpectedEdges.Num(), 2);

	auto Refuse = [&](const TArray<FString>& Declared, const TCHAR* Label)
	{
		TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113101"), &Declared);
		FCortexGraphPreparedPatch Prepared;
		FCortexCommandResult Error;
		const bool bPreviewed = Request.IsValid()
			&& FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Prepared, Error);
		TestFalse(FString::Printf(TEXT("%s is refused [%s: %s]"), Label, *Error.ErrorCode, *Error.ErrorMessage), bPreviewed);
		TestEqual(FString::Printf(TEXT("%s reports INVALID_OPERATION"), Label),
			Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
		TestTrue(FString::Printf(TEXT("%s names the unreviewed or missing consumer"), Label),
			Error.ErrorMessage.Contains(TEXT("missing")) && Error.ErrorMessage.Contains(TEXT("extra")));
	};

	TArray<FString> Missing = { ExpectedEdges[0] };
	Refuse(Missing, TEXT("a reviewed set that omits one live consumer"));
	TArray<FString> Extra = ExpectedEdges;
	Extra.Add(Fixture.Call->NodeGuid.ToString() + TEXT(".then"));
	Refuse(Extra, TEXT("a reviewed set that adds an unreviewed edge"));

	// A refusal never mutates: the fingerprint, the whole native graph and the node count survive.
	TestEqual(TEXT("a refused review leaves the fingerprint"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("a refused review leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("a refused review leaves the node count"),
		Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0, NodesBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 5. The pin contract is strict: a non-orphan stale pin, a missing replacement pin and a
//    non-output replacement pin are all refused before anything is planned
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewirePinContractRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.RefusesWrongPinContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewirePinContractRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewirePins_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);

	struct FCase
	{
		const TCHAR* StalePin;
		const TCHAR* ReplacementPin;
		const TCHAR* ErrorCode;
		const TCHAR* Label;
	};
	const FCase Cases[] = {
		{ TEXT("then"), TEXT("ReturnValue"), TEXT("INVALID_OPERATION"), TEXT("a stale pin that is not an orphan") },
		{ StalePinName, TEXT("NoSuchOutputPin"), TEXT("PIN_NOT_FOUND"), TEXT("a replacement pin that does not exist") },
		{ StalePinName, TEXT("Quest"), TEXT("PIN_NOT_FOUND"), TEXT("a replacement pin that is not an output") },
		{ StalePinName, StalePinName, TEXT("INVALID_FIELD"), TEXT("the same pin named twice") },
	};
	for (const FCase& Case : Cases)
	{
		TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113102"));
		SetRequestSourcePins(Request, Case.StalePin, Case.ReplacementPin);
		FCortexGraphPreparedPatch Prepared;
		FCortexCommandResult Error;
		const bool bPreviewed = Request.IsValid()
			&& FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Prepared, Error);
		TestFalse(FString::Printf(TEXT("%s is refused [%s: %s]"), Case.Label, *Error.ErrorCode, *Error.ErrorMessage), bPreviewed);
		TestEqual(FString::Printf(TEXT("%s reports %s"), Case.Label, Case.ErrorCode),
			Error.ErrorCode, FString(Case.ErrorCode));
	}

	TestEqual(TEXT("a refused pin contract leaves the fingerprint"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("a refused pin contract leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 6. The canonical stale/replacement type compatibility guard is enforced
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireIncompatibleTypeRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.RefusesIncompatibleStaleType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireIncompatibleTypeRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireTypes_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// A second, deliberately type-incompatible in-use orphan output on the same call node: the native
	// pin API is the engine's own (UEdGraphPin is not a UObject in UE 5.8), and MakeLinkTo performs no
	// type arbitration, so the pin really is an in-use orphan whose canonical type differs from the
	// current output.
	UEdGraphPin* const IntOrphan = UEdGraphPin::CreatePin(Fixture.Call);
	TestNotNull(TEXT("the native orphan pin is created"), IntOrphan);
	if (!IntOrphan) { Fixture.Cleanup(); return false; }
	IntOrphan->PinName = FName(TEXT("bStaleCount"));
	IntOrphan->Direction = EGPD_Output;
	IntOrphan->PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	IntOrphan->bOrphanedPin = true;
	IntOrphan->bNotConnectable = true;
	Fixture.Call->Pins.Add(IntOrphan);
	UEdGraphPin* const ConsumedInput = Fixture.EnabledConsumer->FindPin(TEXT("execute"));
	TestNotNull(TEXT("the incompatible orphan pin has a consumer input"), ConsumedInput);
	if (!ConsumedInput) { Fixture.Cleanup(); return false; }
	IntOrphan->MakeLinkTo(ConsumedInput);
	TestEqual(TEXT("the incompatible orphan pin is in use"), IntOrphan->LinkedTo.Num(), 1);

	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113103"));
	SetRequestSourcePins(Request, TEXT("bStaleCount"), ReplacementPinName);
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	FCortexGraphPreparedPatch Prepared;
	FCortexCommandResult Error;
	TestFalse(FString::Printf(TEXT("an int orphan output is refused as stale for a bool output [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage),
		Request.IsValid() && FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Prepared, Error));
	TestEqual(TEXT("the incompatible canonical type reports TYPE_MISMATCH"),
		Error.ErrorCode, FString(CortexErrorCodes::TypeMismatch));
	TestTrue(TEXT("the refusal names the differing canonical dimension"),
		Error.ErrorMessage.Contains(TEXT("category")));
	TestEqual(TEXT("a refused type comparison leaves the fingerprint"),
		LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("a refused type comparison leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 7. A stale preview token or a graph edit after the preview refuses without mutation
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireStaleIntentRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.StaleTokenAndFingerprintRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireStaleIntentRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireStale_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	const TCHAR* const PatchId = TEXT("00000000-0000-0000-0000-000000113104");
	TSharedPtr<FJsonObject> PreviewRequest = RewireRequest(Fixture, PatchId, &ExpectedEdges);
	FCortexGraphPreparedPatch Preview;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the reviewed preview succeeds [%s: %s]"), *Error.ErrorCode, *Error.ErrorMessage),
		PreviewRequest.IsValid() && FCortexGraphPatchOps::Preflight(Fixture.Blueprint, PreviewRequest, Preview, Error));
	const FString ValidToken = Preview.ValidationHash;

	// A token minted for another intent (or a tampered one) is refused by the apply, and nothing moves.
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	TSharedPtr<FJsonObject> WrongTokenRequest = RewireRequest(Fixture, PatchId, &ExpectedEdges,
		TEXT("00000000000000000000000000000000"), false);
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("a token that does not match the current intent fails the apply"),
		WrongTokenRequest.IsValid() && FCortexGraphPatchOps::Execute(Fixture.Blueprint, WrongTokenRequest, Outcome, Error));
	TestEqual(TEXT("a stale token reports STALE_PRECONDITION"),
		Error.ErrorCode, FString(CortexErrorCodes::StalePrecondition));
	TestEqual(TEXT("a stale token mutates nothing"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("a stale token leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);

	// A graph edit after the preview invalidates the fingerprint the token was computed over.
	TSharedPtr<FJsonObject> ApplyRequest = RewireRequest(Fixture, PatchId, &ExpectedEdges, ValidToken, false);
	TestTrue(TEXT("the approved apply request is built"), ApplyRequest.IsValid());
	Fixture.Call->NodePosX += 40;
	FBlueprintEditorUtils::MarkBlueprintAsModified(Fixture.Blueprint);
	FCortexGraphPatchOutcome EditedOutcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("a graph edit after the preview fails the apply"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, ApplyRequest, EditedOutcome, Error));
	TestEqual(TEXT("an edited graph reports STALE_PRECONDITION"),
		Error.ErrorCode, FString(CortexErrorCodes::StalePrecondition));
	// The edit itself is the only difference: the orphan pin and its two consumers are untouched by
	// the refusal.
	UEdGraphPin* const StaleAfter = Fixture.FindCallPin(StalePinName);
	TestNotNull(TEXT("a stale fingerprint leaves the orphan pin in place"), StaleAfter);
	TestEqual(TEXT("a stale fingerprint leaves the orphan pin's consumers in place"),
		StaleAfter ? StaleAfter->LinkedTo.Num() : -1, ExpectedEdges.Num());

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 8. A staged apply (compile=false, save=false) is verified in memory and persists nothing
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireStagedApplyTest,
	"Cortex.Graph.Authoring.Migration.Rewire.StagedApplyWithoutSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireStagedApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireStaged_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	UEdGraphPin* const ReplacementBefore = Fixture.FindCallPin(ReplacementPinName);
	const int32 ReplacementConsumersBefore = ReplacementBefore ? ReplacementBefore->LinkedTo.Num() : -1;

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the staged request is prepared [%s: %s]"), *Error.ErrorCode, *Error.ErrorMessage),
		PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113105"), ExpectedEdges, Request, Error,
			/*bCompile=*/false, /*bSave=*/false));

	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("the staged apply rewires and reads back [%s: %s]"), *Error.ErrorCode, *Error.ErrorMessage),
		Request.IsValid() && FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("the staged apply reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the staged apply does not compile"), Outcome.CompileStatus, FString(TEXT("not_requested")));
	TestEqual(TEXT("the staged apply reads back the native state"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("the staged apply never rolls back"), Outcome.RollbackStatus, FString(TEXT("not_requested")));
	TestEqual(TEXT("the staged apply never saves"), Outcome.SaveStatus, FString(TEXT("not_requested")));
	TestFalse(TEXT("the staged apply claims no persistence"), Outcome.bSaved);
	TestEqual(TEXT("the staged apply runs no target compile"), Outcome.TargetCompileCount, 0);
	TestNotNull(TEXT("the staged apply publishes its inventory"), Outcome.RewireInventory.Get());

	// The native post-state: the orphan pin is gone and the current output owns exactly the reviewed
	// consumers, reciprocally once each.
	TestNull(TEXT("the stale orphan output pin is removed"), Fixture.FindCallPin(StalePinName));
	UEdGraphPin* const Replacement = Fixture.FindCallPin(ReplacementPinName);
	TestNotNull(TEXT("the current output pin remains"), Replacement);
	UEdGraphPin* const GateIn = Fixture.EnabledConsumer->FindPin(TEXT("bEnabled"));
	UEdGraphPin* const ConvertIn = Fixture.BoolToInt->FindPin(TEXT("InBool"));
	TestNotNull(TEXT("the direct consumer input remains"), GateIn);
	TestNotNull(TEXT("the conversion consumer input remains"), ConvertIn);
	TestEqual(TEXT("the current output feeds exactly the reviewed consumers"),
		Replacement ? Replacement->LinkedTo.Num() : -1, ExpectedEdges.Num());
	TestTrue(TEXT("the current output feeds the direct consumer"),
		Replacement && GateIn && Replacement->LinkedTo.Contains(GateIn));
	TestTrue(TEXT("the current output feeds the conversion consumer"),
		Replacement && ConvertIn && Replacement->LinkedTo.Contains(ConvertIn));
	TestTrue(TEXT("the direct consumer is no longer fanned out"),
		GateIn && GateIn->LinkedTo.Num() == 1 && GateIn->LinkedTo[0] == Replacement);
	TestTrue(TEXT("the conversion consumer is no longer fanned out"),
		ConvertIn && ConvertIn->LinkedTo.Num() == 1 && ConvertIn->LinkedTo[0] == Replacement);
	TestTrue(TEXT("the exec chain survives the repair"), Fixture.IsExecChainIntact());
	TestTrue(TEXT("the staged apply leaves the package dirty"), Fixture.Package->IsDirty());
	TestEqual(TEXT("the current output started the staged apply without a consumer"),
		ReplacementConsumersBefore, 0);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 9. A readback divergence refuses and still restores the orphan pin with its flags and links
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireReadbackFaultTest,
	"Cortex.Graph.Authoring.Migration.Rewire.ReadbackDivergenceRestoresOrphanPin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireReadbackFaultTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireReadback_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString ConsumersBefore = ConsumerPinState(Fixture);
	UEdGraphPin* const StaleBefore = Fixture.FindHistoricOutputPin();
	const FGuid OrphanPinIdBefore = StaleBefore ? StaleBefore->PinId : FGuid();
	TestTrue(TEXT("the fixture orphan pin has a valid identity"), OrphanPinIdBefore.IsValid());

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the staged request is prepared [%s: %s]"), *Error.ErrorCode, *Error.ErrorMessage),
		PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113106"), ExpectedEdges, Request, Error,
			/*bCompile=*/false, /*bSave=*/false));

	// The readback seam fails the named comparison after it really ran: the mutation itself succeeded, so
	// the coordinator must roll the real change back and prove the orphan pin came back with it.
	FCortexGraphMigrationOps::SetCallOutputReadbackFaultForTesting(TEXT("call_output_after_edges"));
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("an injected readback divergence fails the apply"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	FCortexGraphMigrationOps::ClearCallOutputReadbackFaultForTesting();
	TestEqual(TEXT("the injected divergence reports a mismatched readback"),
		Outcome.ReadbackStatus, FString(TEXT("mismatched")));
	TestEqual(TEXT("the failed readback still reports the applied mutation"),
		Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the failed readback restores the transaction"),
		Outcome.RollbackStatus, FString(TEXT("restored")));
	TestFalse(TEXT("the restored asset is not blocked"), Outcome.bBlocked);

	// The independent native oracle again: the fingerprint cannot see the orphan flags or the pin
	// identity, and a recreated pin with a fresh GUID breaks every existing pin-ID reference.
	UEdGraphPin* const Stale = Fixture.FindHistoricOutputPin();
	TestNotNull(TEXT("the orphan pin is restored"), Stale);
	TestTrue(TEXT("the orphan flag is restored"), Stale && Stale->bOrphanedPin);
	TestTrue(TEXT("the not-connectable flag is restored"), Stale && Stale->bNotConnectable);
	TestTrue(FString::Printf(TEXT("the restored orphan pin keeps its original PinId '%s'"),
		*OrphanPinIdBefore.ToString()), Stale && Stale->PinId == OrphanPinIdBefore);
	TestEqual(TEXT("both original consumer links are restored"),
		Stale ? FString::Join(Fixture.OrphanEdgeEndpoints(), TEXT("\n")) : FString(),
		FString::Join(ExpectedEdges, TEXT("\n")));
	TestEqual(TEXT("the reviewed consumers' pin state is restored"), ConsumerPinState(Fixture), ConsumersBefore);
	TestEqual(TEXT("every node, pin, flag and link is restored"), CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the fingerprint is restored"), LiveGraphHash(Fixture.Blueprint), HashBefore);

	Fixture.Cleanup();
	return true;
}
// ---------------------------------------------------------------------------
// 10. A historic output that shares its name with a surviving input is restored by direction
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireSameNamePinDirectionTest,
	"Cortex.Graph.Authoring.Migration.Rewire.SameNameInputAndOrphanOutputRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireSameNamePinDirectionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("same-name signature fixture is created"),
		Fixture.Build(TEXT("WBP_MigrationRewireSameName_T113"), EFixtureSignature::SameNameInputParameter));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// Fixture fidelity: the migrated declaration really carries an input and an in-use orphan output
	// under one name, so a name-only pin lookup cannot tell the two apart.
	UEdGraphPin* const OrphanOutput = Fixture.Call
		? Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Output) : nullptr;
	UEdGraphPin* const SurvivingInput = Fixture.Call
		? Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Input) : nullptr;
	TestNotNull(TEXT("the same-named input pin of the migrated declaration exists"), SurvivingInput);
	TestNotNull(TEXT("the same-named historic output pin is retained as an orphan"), OrphanOutput);
	TestTrue(TEXT("the retained output pin is orphaned"), OrphanOutput && OrphanOutput->bOrphanedPin);
	TestTrue(TEXT("the retained output pin is not connectable"), OrphanOutput && OrphanOutput->bNotConnectable);
	TestEqual(TEXT("the retained output pin keeps both reviewed consumers"),
		OrphanOutput ? OrphanOutput->LinkedTo.Num() : 0, 2);
	TestTrue(TEXT("the surviving input pin is not the orphan"), SurvivingInput && !SurvivingInput->bOrphanedPin);
	TestEqual(TEXT("the surviving input pin carries no link"),
		SurvivingInput ? SurvivingInput->LinkedTo.Num() : -1, 0);
	TestEqual(TEXT("the fixture asset is a real compiler error"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	if (!OrphanOutput || !SurvivingInput) { Fixture.Cleanup(); return false; }

	const FGuid OrphanPinIdBefore = OrphanOutput->PinId;
	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString ConsumersBefore = ConsumerPinState(Fixture);
	const bool bDirtyBefore = Fixture.Package->IsDirty();
	TestEqual(TEXT("the fixture oracle finds both reviewed consumers"), ExpectedEdges.Num(), 2);
	if (ExpectedEdges.Num() != 2) { Fixture.Cleanup(); return false; }

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	const bool bApproved = PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113107"),
		ExpectedEdges, Request, Error, /*bCompile=*/false, /*bSave=*/false);
	TestTrue(FString::Printf(TEXT("the same-name approved apply prepares [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bApproved);
	if (!bApproved) { Fixture.Cleanup(); return false; }

	FCortexGraphPatchOps::SetApplyFaultPointForTesting(TEXT("migration_rewire_after_removal"));
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestFalse(TEXT("the injected fault fails the apply"),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	FCortexGraphPatchOps::SetApplyFaultPointForTesting(NAME_None);

	TestEqual(TEXT("the same-name rollback is restored"), Outcome.RollbackStatus, FString(TEXT("restored")));
	TestFalse(TEXT("the same-name rollback does not block the asset"), Outcome.bBlocked);

	// The independent native oracle: the restored pin must be the output (by direction and identity),
	// while the surviving same-named input stays an untouched input with its own identity.
	UEdGraphPin* const RestoredOutput = Fixture.Call
		? Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Output) : nullptr;
	UEdGraphPin* const RestoredInput = Fixture.Call
		? Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Input) : nullptr;
	TestNotNull(TEXT("the same-named orphan output is recreated"), RestoredOutput);
	TestTrue(FString::Printf(TEXT("the recreated orphan output keeps its original PinId '%s'"),
		*OrphanPinIdBefore.ToString()), RestoredOutput && RestoredOutput->PinId == OrphanPinIdBefore);
	TestTrue(TEXT("the recreated orphan output keeps its orphan flag"),
		RestoredOutput && RestoredOutput->bOrphanedPin);
	TestTrue(TEXT("the recreated orphan output keeps its not-connectable flag"),
		RestoredOutput && RestoredOutput->bNotConnectable);
	TestEqual(TEXT("both original consumer links are restored"),
		RestoredOutput ? FString::Join(Fixture.OrphanEdgeEndpoints(), TEXT("\n")) : FString(),
		FString::Join(ExpectedEdges, TEXT("\n")));
	TestNotNull(TEXT("the same-named input pin survives the rollback"), RestoredInput);
	TestTrue(TEXT("the surviving input pin is never adopted as the restored orphan"),
		RestoredInput && !RestoredInput->bOrphanedPin && RestoredInput->LinkedTo.Num() == 0);
	TestEqual(TEXT("every node, pin, flag and link is restored"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("both reviewed consumer states are restored"), ConsumerPinState(Fixture), ConsumersBefore);
	TestEqual(TEXT("the fingerprint is restored"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("the dirty baseline is restored"), Fixture.Package->IsDirty(), bDirtyBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 11. A replacement output the target function does not declare is refused before approval
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireNonReflectedOutputRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.RefusesNonReflectedReplacementOutput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireNonReflectedOutputRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireReflected_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	UFunction* const Reflected = UCortexGraphRewireFixtureWidget::StaticClass()
		->FindFunctionByName(TEXT("UpdateFromQuestRequirements"));
	TestNotNull(TEXT("the mirrored current declaration resolves for the call node"), Reflected);
	if (!Reflected) { Fixture.Cleanup(); return false; }

	// A live, connectable output pin of the same canonical type that the reflected declaration does
	// not declare. The native pin API is the engine's own (UEdGraphPin is not a UObject in UE 5.8), so
	// the pin really is present on the node and really is a non-orphan output a caller could select.
	UEdGraphPin* const Fabricated = UEdGraphPin::CreatePin(Fixture.Call);
	TestNotNull(TEXT("the fabricated replacement output is created"), Fabricated);
	if (!Fabricated) { Fixture.Cleanup(); return false; }
	Fabricated->PinName = FName(TEXT("bHandledByHistoricSignature"));
	Fabricated->Direction = EGPD_Output;
	Fabricated->PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	Fixture.Call->Pins.Add(Fabricated);
	TestFalse(TEXT("the fabricated replacement output is not an orphan"), Fabricated->bOrphanedPin);
	TestFalse(TEXT("the fabricated replacement output is not marked unconnectable"), Fabricated->bNotConnectable);
	TestEqual(TEXT("the fabricated replacement output starts unconnected"), Fabricated->LinkedTo.Num(), 0);

	// The independent oracle: the reflected declaration really declares no property of that name, so
	// the compiler could never produce the value a rewire onto this pin would claim.
	bool bDeclaredByFunction = false;
	for (TFieldIterator<FProperty> It(Reflected); It; ++It)
	{
		if (It->GetFName() == Fabricated->PinName)
		{
			bDeclaredByFunction = true;
			break;
		}
	}
	TestFalse(TEXT("the reflected declaration declares no such output"), bDeclaredByFunction);

	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);

	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113108"));
	TestTrue(TEXT("the fabricated-pin request is built"),
		SetRequestSourcePins(Request, *Fixture.HistoricPinName.ToString(), TEXT("bHandledByHistoricSignature")));
	FCortexGraphPreparedPatch Prepared;
	FCortexCommandResult Error;
	const bool bPreviewed = Request.IsValid()
		&& FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Prepared, Error);
	TestFalse(FString::Printf(
		TEXT("a replacement output the reflected declaration does not declare is refused [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bPreviewed);
	TestTrue(TEXT("the refusal reports a missing operation or a missing pin"),
		Error.ErrorCode == FString(CortexErrorCodes::InvalidOperation)
			|| Error.ErrorCode == FString(CortexErrorCodes::PinNotFound));
	TestTrue(TEXT("the refusal names the undeclared replacement pin"),
		Error.ErrorMessage.Contains(TEXT("bHandledByHistoricSignature")));
	TestEqual(TEXT("a refused non-reflected replacement leaves the fingerprint"),
		LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("a refused non-reflected replacement leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 12. An absent orphan with an empty reviewed edge set is a mistargeted request, not a replay
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireAbsentOrphanEmptyReviewRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.RefusesEmptyReviewWithoutOrphan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireAbsentOrphanEmptyReviewRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	// This fixture never had a historic output, so its single compile succeeds and no "In use pin"
	// diagnostic is expected at all: any compile error would fail the test on its own.

	FFixture Fixture;
	TestTrue(TEXT("no-orphan fixture is created"),
		Fixture.Build(TEXT("WBP_MigrationRewireNoOrphan_T113"), EFixtureSignature::CurrentOnly));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// The clean whole-asset compile is the proof that this fixture carries no compiler error at all.
	TestEqual(TEXT("the no-orphan fixture compiles clean"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_UpToDate));

	TestNull(TEXT("a current-declaration call carries no historic orphan output"),
		Fixture.Call ? Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Output) : nullptr);
	UEdGraphPin* const Replacement = Fixture.Call
		? Fixture.Call->FindPin(FName(ReplacementPinName), EGPD_Output) : nullptr;
	TestNotNull(TEXT("the current return output exists"), Replacement);
	TestEqual(TEXT("the current return output starts unconnected"),
		Replacement ? Replacement->LinkedTo.Num() : -1, 0);

	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const int32 NodesBefore = Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0;

	// An absent orphan plus an empty reviewed edge set satisfies every replay equality: the loop runs
	// zero times and the unconnected replacement output matches zero discovered consumers, so a
	// request that repairs nothing would be approved as a complete, unchanged repair.
	const TArray<FString> EmptyReview;
	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113109"), &EmptyReview);
	const TArray<TSharedPtr<FJsonValue>>* DeclaredEdges = nullptr;
	const TSharedPtr<FJsonObject>* Migration = nullptr;
	TestTrue(TEXT("the request really carries an empty migration.edges array"),
		Request.IsValid() && Request->TryGetObjectField(TEXT("migration"), Migration) && Migration
			&& (*Migration)->TryGetArrayField(TEXT("edges"), DeclaredEdges) && DeclaredEdges
			&& DeclaredEdges->IsEmpty());

	FCortexGraphPreparedPatch Prepared;
	FCortexCommandResult Error;
	const bool bPreviewed = Request.IsValid()
		&& FCortexGraphPatchOps::Preflight(Fixture.Blueprint, Request, Prepared, Error);
	TestFalse(FString::Printf(
		TEXT("an absent orphan with an empty reviewed edge set is refused [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bPreviewed);
	TestEqual(TEXT("the empty-review mistarget reports INVALID_OPERATION"),
		Error.ErrorCode, FString(CortexErrorCodes::InvalidOperation));
	TestTrue(TEXT("the refusal names the absent historic pin it was asked to replay"),
		Error.ErrorMessage.Contains(Fixture.HistoricPinName.ToString()));

	// Behavioral follow-through: whenever this request is (incorrectly) approved, it must still never
	// be applied as a successful repair, because nothing was ever repaired.
	if (bPreviewed)
	{
		TSharedPtr<FJsonObject> ApplyRequest = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113109"),
			&EmptyReview, Prepared.ValidationHash, false);
		FCortexGraphPatchOutcome Outcome;
		Error = FCortexCommandResult();
		const bool bApplied = ApplyRequest.IsValid()
			&& FCortexGraphPatchOps::Execute(Fixture.Blueprint, ApplyRequest, Outcome, Error);
		TestFalse(FString::Printf(
			TEXT("an approved empty-review mistarget is still not reported as a repair [%s: %s]"),
			*Error.ErrorCode, *Error.ErrorMessage), bApplied);
		TestTrue(TEXT("the mistargeted apply never claims an unchanged successful repair"),
			Outcome.ApplyStatus != TEXT("unchanged"));
	}

	TestEqual(TEXT("a refused empty review leaves the fingerprint"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("a refused empty review leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("a refused empty review leaves the node count"),
		Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0, NodesBefore);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 13. A repair that compiles is verified in memory and persists nothing
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireCompiledRepairTest,
	"Cortex.Graph.Authoring.Migration.Rewire.CompiledRepairWithoutSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireCompiledRepairTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireCompiled_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle finds both reviewed consumers"), ExpectedEdges.Num(), 2);
	TestEqual(TEXT("the fixture is a real compiler error before the repair"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	const FGuid CallGuid = Fixture.Call ? Fixture.Call->NodeGuid : FGuid();
	const FGuid GateConsumerGuid = Fixture.EnabledConsumer ? Fixture.EnabledConsumer->NodeGuid : FGuid();
	const FGuid ConvertConsumerGuid = Fixture.BoolToInt ? Fixture.BoolToInt->NodeGuid : FGuid();
	const FString HistoricPin = Fixture.HistoricPinName.ToString();
	if (ExpectedEdges.Num() != 2 || !CallGuid.IsValid()) { Fixture.Cleanup(); return false; }

	// The compiler-invalid fixture is persisted first: a non-persisting repair must leave the disk
	// file byte-identical instead of only claiming it saved nothing.
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("the compiler-invalid fixture is persisted as the disk baseline"), Fixture.SaveToDisk());
	const TArray<uint8> DiskBefore = ReadBytes(Filename);
	TestTrue(TEXT("the disk baseline really exists"), DiskBefore.Num() > 0);

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the compiled repair request is prepared [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage),
		PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113110"), ExpectedEdges, Request, Error,
			/*bCompile=*/true, /*bSave=*/false));

	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	const bool bApplied = Request.IsValid()
		&& FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error);
	Operations.End();
	TestTrue(FString::Printf(TEXT("the repair compiles and reads back [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bApplied);
	TestEqual(TEXT("the compiled repair reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the compiled repair reports compiled"), Outcome.CompileStatus, FString(TEXT("compiled")));
	TestEqual(TEXT("the compiled repair reads back the native state"),
		Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("the compiled repair never rolls back"),
		Outcome.RollbackStatus, FString(TEXT("not_requested")));
	TestEqual(TEXT("the compiled repair is never persisted"),
		Outcome.SaveStatus, FString(TEXT("not_requested")));
	TestFalse(TEXT("the compiled repair claims no persistence"), Outcome.bSaved);
	TestFalse(TEXT("the compiled repair does not block the asset"), Outcome.bBlocked);
	TestEqual(TEXT("exactly one target compile is reported"), Outcome.TargetCompileCount, 1);
	TestEqual(TEXT("exactly one target compile is observed"), Operations.TargetCompiles, 1);
	TestEqual(TEXT("a non-persisting repair commits no save"), Operations.Saves, 0);
	// The whole-asset compile result is the fixture's proof that it carried no unrelated compiler
	// error: the engine has now accepted the entire asset, not only the repaired call.
	TestEqual(TEXT("the repaired asset compiles clean"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_UpToDate));

	// Post-compile native state, re-resolved by identity because a compile may rebuild pins.
	UEdGraphNode* const CallAfter = FindNodeInGraph(Fixture.Graph, CallGuid);
	TestNotNull(TEXT("the planned call node still resolves after the target compile"), CallAfter);
	if (CallAfter)
	{
		TestNull(TEXT("the stale orphan output pin is removed"), CallAfter->FindPin(FName(*HistoricPin)));
		UEdGraphPin* const Replacement = CallAfter->FindPin(FName(ReplacementPinName), EGPD_Output);
		TestNotNull(TEXT("the replacement output pin remains"), Replacement);
		UEdGraphNode* const GateConsumer = FindNodeInGraph(Fixture.Graph, GateConsumerGuid);
		UEdGraphNode* const ConvertConsumer = FindNodeInGraph(Fixture.Graph, ConvertConsumerGuid);
		UEdGraphPin* const GateIn = GateConsumer ? GateConsumer->FindPin(TEXT("bEnabled")) : nullptr;
		UEdGraphPin* const ConvertIn = ConvertConsumer ? ConvertConsumer->FindPin(TEXT("InBool")) : nullptr;
		TestNotNull(TEXT("the direct consumer input resolves"), GateIn);
		TestNotNull(TEXT("the conversion consumer input resolves"), ConvertIn);
		TestEqual(TEXT("the replacement output feeds exactly the reviewed consumers"),
			Replacement ? Replacement->LinkedTo.Num() : -1, ExpectedEdges.Num());
		TestTrue(TEXT("the replacement output feeds the direct consumer"),
			Replacement && GateIn && Replacement->LinkedTo.Contains(GateIn));
		TestTrue(TEXT("the replacement output feeds the conversion consumer"),
			Replacement && ConvertIn && Replacement->LinkedTo.Contains(ConvertIn));
		UEdGraphPin* const ExecuteIn = CallAfter->FindPin(TEXT("execute"));
		UEdGraphPin* const ThenOut = CallAfter->FindPin(TEXT("then"));
		TestEqual(TEXT("the compiled call stays on the entry's exec chain"),
			ExecuteIn ? ExecuteIn->LinkedTo.Num() : -1, 1);
		TestEqual(TEXT("the compiled call starts the consumer exec chain"),
			ThenOut ? ThenOut->LinkedTo.Num() : -1, 1);
	}
	TestTrue(TEXT("the compiled repair leaves the package dirty"), Fixture.Package->IsDirty());
	TestTrue(TEXT("the non-persisting repair leaves the disk bytes untouched"),
		SameBytes(DiskBefore, ReadBytes(Filename)));

	Fixture.Cleanup();
	IFileManager::Get().Delete(*Filename, false, true, true);
	return true;
}

// ---------------------------------------------------------------------------
// 14. An explicit save commits the verified repair and the repaired graph survives a reload
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireSavedRepairTest,
	"Cortex.Graph.Authoring.Migration.Rewire.SavedRepairPersistsToDisk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireSavedRepairTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	// The repair compiles twice: the target compile and the load-time regeneration of the reloaded
	// asset. Only the fixture's own compile is the expected in-use-orphan error, so require at least
	// one occurrence instead of an exact count.
	ExpectFixtureCompileError(*this, 0);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireSaved_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle finds both reviewed consumers"), ExpectedEdges.Num(), 2);
	TestEqual(TEXT("the fixture is a real compiler error before the repair"),
		static_cast<int32>(Fixture.Blueprint->Status), static_cast<int32>(BS_Error));
	const FString PackageName = Fixture.Package->GetName();
	const FString BlueprintName = Fixture.Blueprint->GetName();
	const FGuid GraphGuid = Fixture.Graph ? Fixture.Graph->GraphGuid : FGuid();
	const FGuid CallGuid = Fixture.Call ? Fixture.Call->NodeGuid : FGuid();
	const FGuid GateConsumerGuid = Fixture.EnabledConsumer ? Fixture.EnabledConsumer->NodeGuid : FGuid();
	const FGuid ConvertConsumerGuid = Fixture.BoolToInt ? Fixture.BoolToInt->NodeGuid : FGuid();
	const FString HistoricPin = Fixture.HistoricPinName.ToString();
	if (ExpectedEdges.Num() != 2 || !CallGuid.IsValid()) { Fixture.Cleanup(); return false; }

	// A save=true apply requires a clean starting package, so the compiler-invalid fixture is persisted
	// first: that file is the real baseline and the reviewed starting state.
	const FString Filename = Fixture.Filename();
	IFileManager::Get().Delete(*Filename, false, true, true);
	TestTrue(TEXT("the compiler-invalid fixture is persisted as a clean starting package"), Fixture.SaveToDisk());
	const TArray<uint8> DiskBefore = ReadBytes(Filename);
	TestTrue(TEXT("the disk baseline really exists"), DiskBefore.Num() > 0);
	TestFalse(TEXT("the package begins clean"), Fixture.Package->IsDirty());

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	TestTrue(FString::Printf(TEXT("the saved repair request is prepared [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage),
		PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113111"), ExpectedEdges, Request, Error,
			/*bCompile=*/true, /*bSave=*/true));

	FOperations Operations;
	Operations.Begin();
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	const bool bApplied = Request.IsValid()
		&& FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error);
	Operations.End();
	TestTrue(FString::Printf(TEXT("the repair compiles, reads back and saves [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bApplied);
	TestEqual(TEXT("the saved repair reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the saved repair reports compiled"), Outcome.CompileStatus, FString(TEXT("compiled")));
	TestEqual(TEXT("the saved repair reads back the native state"),
		Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("the saved repair reports saved"), Outcome.SaveStatus, FString(TEXT("saved")));
	TestEqual(TEXT("the saved repair verifies the committed file"),
		Outcome.PostSaveStatus, FString(TEXT("verified")));
	TestTrue(TEXT("the saved repair claims persistence"), Outcome.bSaved);
	TestFalse(TEXT("the saved repair does not block the asset"), Outcome.bBlocked);
	TestEqual(TEXT("exactly one target compile is observed"), Operations.TargetCompiles, 1);
	TestEqual(TEXT("exactly one explicit package save occurs"), Operations.Saves, 1);
	TestFalse(TEXT("the verified saved package is clean"), Fixture.Package->IsDirty());
	const TArray<uint8> DiskAfter = ReadBytes(Filename);
	TestTrue(TEXT("the committed file carries content"), DiskAfter.Num() > 0);
	TestFalse(TEXT("the committed bytes carry the repair, not the compiler-invalid baseline"),
		SameBytes(DiskBefore, DiskAfter));

	UPackage* const PackageBeforeReload = Fixture.Package;
	UBlueprint* const BlueprintBeforeReload = Fixture.Blueprint;
	TArray<UPackage*> PackagesToReload;
	PackagesToReload.Add(PackageBeforeReload);
	FText ReloadError;
	const bool bReloaded = UPackageTools::ReloadPackages(
		PackagesToReload, ReloadError, EReloadPackagesInteractionMode::AssumeNegative);
	TestTrue(FString::Printf(TEXT("the saved repair package reloads: %s"), *ReloadError.ToString()), bReloaded);
	UPackage* const ReloadedPackage = FindPackage(nullptr, *PackageName);
	UWidgetBlueprint* const Reloaded = ReloadedPackage
		? FindObject<UWidgetBlueprint>(ReloadedPackage, *BlueprintName) : nullptr;
	TestNotNull(TEXT("the saved Widget Blueprint resolves after the reload"), Reloaded);
	if (Reloaded)
	{
		TestTrue(TEXT("the reload replaced the in-memory Blueprint instance"), Reloaded != BlueprintBeforeReload);
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
		TestNotNull(TEXT("the repaired function graph resolves from disk"), ReloadedGraph);
		UEdGraphNode* const ReloadedCall = FindNodeInGraph(ReloadedGraph, CallGuid);
		TestNotNull(TEXT("the repaired call node resolves from disk"), ReloadedCall);
		if (ReloadedCall)
		{
			TestNull(TEXT("the stale orphan output pin is absent after the reload"),
				ReloadedCall->FindPin(FName(*HistoricPin)));
			UEdGraphPin* const Replacement = ReloadedCall->FindPin(FName(ReplacementPinName), EGPD_Output);
			TestNotNull(TEXT("the replacement output pin resolves after the reload"), Replacement);
			UEdGraphNode* const GateConsumer = FindNodeInGraph(ReloadedGraph, GateConsumerGuid);
			UEdGraphNode* const ConvertConsumer = FindNodeInGraph(ReloadedGraph, ConvertConsumerGuid);
			UEdGraphPin* const GateIn = GateConsumer ? GateConsumer->FindPin(TEXT("bEnabled")) : nullptr;
			UEdGraphPin* const ConvertIn = ConvertConsumer ? ConvertConsumer->FindPin(TEXT("InBool")) : nullptr;
			TestNotNull(TEXT("the direct consumer resolves from disk"), GateIn);
			TestNotNull(TEXT("the conversion consumer resolves from disk"), ConvertIn);
			TestEqual(TEXT("the persisted replacement output feeds exactly the reviewed consumers"),
				Replacement ? Replacement->LinkedTo.Num() : -1, ExpectedEdges.Num());
			TestTrue(TEXT("the persisted replacement output feeds the direct consumer"),
				Replacement && GateIn && Replacement->LinkedTo.Contains(GateIn));
			TestTrue(TEXT("the persisted replacement output feeds the conversion consumer"),
				Replacement && ConvertIn && Replacement->LinkedTo.Contains(ConvertIn));
			UEdGraphPin* const ExecuteIn = ReloadedCall->FindPin(TEXT("execute"));
			UEdGraphPin* const ThenOut = ReloadedCall->FindPin(TEXT("then"));
			TestEqual(TEXT("the persisted call stays on the entry's exec chain"),
				ExecuteIn ? ExecuteIn->LinkedTo.Num() : -1, 1);
			TestEqual(TEXT("the persisted call starts the consumer exec chain"),
				ThenOut ? ThenOut->LinkedTo.Num() : -1, 1);
		}
	}

	// The reload replaced the in-memory objects, so cleanup owns the reloaded package instead.
	if (GEditor && GEditor->Trans)
	{
		GEditor->Trans->Reset(FText::FromString(TEXT("CortexGraphMigrationRewireSavedRepairCleanup")));
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
	TestTrue(TEXT("the saved repair fixture file is removed"),
		IFileManager::Get().Delete(*Filename, false, true, true));
	return true;
}

// ---------------------------------------------------------------------------
// 15. A reviewed consumer whose callback rewrites its own pins is rolled back exactly
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireConsumerSideEffectRollbackTest,
	"Cortex.Graph.Authoring.Migration.Rewire.ConsumerPinSideEffectRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireConsumerSideEffectRollbackTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireConsumer_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }
	TestTrue(TEXT("the pin-changing Copy consumer is added"), Fixture.AddCopyConsumer());
	if (!Fixture.CopyConsumer) { Fixture.Cleanup(); return false; }

	// Fixture fidelity: both Copy pins are wildcards and the reviewed input is the only link the repair
	// replaces, so nothing but the consumer's own connection callback can retype its output pin.
	TestEqual(TEXT("the Copy consumer input starts wildcard"),
		CopyPinCategory(Fixture, UEdGraphSchema_K2::PN_Item, EGPD_Input),
		FString(UEdGraphSchema_K2::PC_Wildcard.ToString()));
	TestEqual(TEXT("the Copy consumer output starts wildcard"),
		CopyPinCategory(Fixture, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
		FString(UEdGraphSchema_K2::PC_Wildcard.ToString()));
	TestNotNull(TEXT("the Copy consumer input resolves"), Fixture.CopyConsumer->FindPin(UEdGraphSchema_K2::PN_Item, EGPD_Input));

	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle now finds all three reviewed consumers"), ExpectedEdges.Num(), 3);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString ConsumersBefore = ConsumerPinState(Fixture);
	const bool bDirtyBefore = Fixture.Package->IsDirty();
	UEdGraphPin* const StaleBefore = Fixture.FindHistoricOutputPin();
	const FGuid OrphanPinIdBefore = StaleBefore ? StaleBefore->PinId : FGuid();
	TestTrue(TEXT("the fixture orphan pin has a valid identity"), OrphanPinIdBefore.IsValid());

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	const bool bApproved = PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113112"),
		ExpectedEdges, Request, Error, /*bCompile=*/false, /*bSave=*/false);
	TestTrue(FString::Printf(TEXT("the approved apply request prepares [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bApproved);
	if (!bApproved) { Fixture.Cleanup(); return false; }

	// The rewire itself succeeds, but reconnecting this consumer makes the engine's own callback
	// conform its wildcard output to the reconnected value: state the caller never reviewed and never
	// approved. The in-place readback therefore refuses the repair, and the coordinator must roll the
	// real mutation back instead of reporting a verified repair.
	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	const bool bApplied = FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error);
	TestFalse(FString::Printf(TEXT("the unreviewed consumer mutation is refused [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bApplied);
	TestEqual(TEXT("the rewired graph really was applied before the readback"),
		Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the unreviewed consumer mutation reports a mismatched readback"),
		Outcome.ReadbackStatus, FString(TEXT("mismatched")));
	TestEqual(TEXT("the consumer side effect is rolled back"), Outcome.RollbackStatus, FString(TEXT("restored")));
	TestFalse(TEXT("the restored asset is not blocked"), Outcome.bBlocked);

	// The independent native oracle: the consumer's own callback retyped both of its pins, so the
	// rollback has to bring back both wildcards, the reset default and the reviewed input link.
	TestEqual(TEXT("the consumer input is wildcard again"),
		CopyPinCategory(Fixture, UEdGraphSchema_K2::PN_Item, EGPD_Input),
		FString(UEdGraphSchema_K2::PC_Wildcard.ToString()));
	TestEqual(TEXT("the consumer output is wildcard again"),
		CopyPinCategory(Fixture, UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output),
		FString(UEdGraphSchema_K2::PC_Wildcard.ToString()));
	UEdGraphPin* const ItemAfter = Fixture.CopyConsumer->FindPin(UEdGraphSchema_K2::PN_Item, EGPD_Input);
	UEdGraphPin* const ResultAfter = Fixture.CopyConsumer->FindPin(UEdGraphSchema_K2::PN_ReturnValue, EGPD_Output);
	UEdGraphPin* const OrphanAfter = Fixture.FindHistoricOutputPin();
	TestNotNull(TEXT("the orphan pin is restored"), OrphanAfter);
	TestTrue(TEXT("the consumer input is linked to the restored orphan again"),
		ItemAfter && OrphanAfter && ItemAfter->LinkedTo.Num() == 1 && ItemAfter->LinkedTo[0] == OrphanAfter);
	TestTrue(TEXT("the consumer output stays unconnected"), ResultAfter && ResultAfter->LinkedTo.IsEmpty());
	TestTrue(TEXT("the orphan flag is restored"), OrphanAfter && OrphanAfter->bOrphanedPin);
	TestTrue(TEXT("the not-connectable flag is restored"), OrphanAfter && OrphanAfter->bNotConnectable);
	TestTrue(FString::Printf(TEXT("the restored orphan pin keeps its original PinId '%s'"),
		*OrphanPinIdBefore.ToString()), OrphanAfter && OrphanAfter->PinId == OrphanPinIdBefore);
	TestEqual(TEXT("all three original consumer links are restored"),
		OrphanAfter ? FString::Join(Fixture.OrphanEdgeEndpoints(), TEXT("\n")) : FString(),
		FString::Join(ExpectedEdges, TEXT("\n")));
	TestEqual(TEXT("every reviewed consumer's pin state is restored"), ConsumerPinState(Fixture), ConsumersBefore);
	TestEqual(TEXT("every node, pin, flag and link is restored"), CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the fingerprint is restored"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("the dirty baseline is restored"), Fixture.Package->IsDirty(), bDirtyBefore);

	Fixture.Cleanup();
	return true;
}
// ---------------------------------------------------------------------------
// 16. A successful apply removes the orphan output even when a surviving input carries its name
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireSameNamePinApplyTest,
	"Cortex.Graph.Authoring.Migration.Rewire.SameNameInputAndOrphanOutputApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireSameNamePinApplyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("same-name signature fixture is created"),
		Fixture.Build(TEXT("WBP_MigrationRewireSameNameApply_T113"), EFixtureSignature::SameNameInputParameter));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// Fixture fidelity: one name carries a surviving input *and* the in-use orphan output this repair
	// removes. That is exactly the shape a name-only removal check misreads as a failed removal, so the
	// successful path has to be proved end to end and not only through an injected-fault rollback.
	UEdGraphPin* const OrphanOutput = Fixture.FindHistoricOutputPin();
	UEdGraphPin* const SurvivingInput = Fixture.Call ? Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Input) : nullptr;
	TestNotNull(TEXT("the same-named input pin of the migrated declaration exists"), SurvivingInput);
	TestNotNull(TEXT("the same-named historic output pin is retained as an orphan"), OrphanOutput);
	TestTrue(TEXT("the retained output pin is orphaned"), OrphanOutput && OrphanOutput->bOrphanedPin);
	TestEqual(TEXT("the retained output pin keeps both reviewed consumers"),
		OrphanOutput ? OrphanOutput->LinkedTo.Num() : -1, 2);
	TestTrue(TEXT("the surviving input pin is not the orphan"),
		SurvivingInput && !SurvivingInput->bOrphanedPin && SurvivingInput->LinkedTo.Num() == 0);
	if (!OrphanOutput || !SurvivingInput) { Fixture.Cleanup(); return false; }

	const FString SurvivingInputBefore = CapturePinRecord(SurvivingInput);
	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle finds both reviewed consumers"), ExpectedEdges.Num(), 2);

	TSharedPtr<FJsonObject> Request;
	FCortexCommandResult Error;
	const bool bApproved = PrepareRewireApply(Fixture, TEXT("00000000-0000-0000-0000-000000113113"),
		ExpectedEdges, Request, Error, /*bCompile=*/false, /*bSave=*/false);
	TestTrue(FString::Printf(TEXT("the same-name approved apply prepares [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage), bApproved);
	if (!bApproved) { Fixture.Cleanup(); return false; }

	FCortexGraphPatchOutcome Outcome;
	Error = FCortexCommandResult();
	TestTrue(FString::Printf(TEXT("the same-name approved apply rewires and reads back [%s: %s]"),
		*Error.ErrorCode, *Error.ErrorMessage),
		FCortexGraphPatchOps::Execute(Fixture.Blueprint, Request, Outcome, Error));
	TestEqual(TEXT("the same-name apply reports applied"), Outcome.ApplyStatus, FString(TEXT("applied")));
	TestEqual(TEXT("the same-name apply reads back the native state"), Outcome.ReadbackStatus, FString(TEXT("matched")));
	TestEqual(TEXT("the same-name apply never rolls back"), Outcome.RollbackStatus, FString(TEXT("not_requested")));
	TestFalse(TEXT("the same-name apply claims no persistence"), Outcome.bSaved);

	// The removal is resolved by name *and* direction: the orphan output is gone while the same-named
	// input survives as its own untouched pin.
	TestNull(TEXT("the stale orphan output pin is removed by the successful apply"),
		Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Output));
	UEdGraphPin* const RestoredInput = Fixture.Call->FindPin(Fixture.HistoricPinName, EGPD_Input);
	TestNotNull(TEXT("the same-named surviving input is not removed with the orphan"), RestoredInput);
	TestEqual(TEXT("the surviving input is untouched by the repair"),
		CapturePinRecord(RestoredInput), SurvivingInputBefore);

	UEdGraphPin* const Replacement = Fixture.FindCallPin(ReplacementPinName);
	UEdGraphPin* const GateIn = Fixture.EnabledConsumer->FindPin(TEXT("bEnabled"));
	UEdGraphPin* const ConvertIn = Fixture.BoolToInt->FindPin(TEXT("InBool"));
	TestNotNull(TEXT("the current output pin remains"), Replacement);
	TestEqual(TEXT("the current output feeds exactly the reviewed consumers"),
		Replacement ? Replacement->LinkedTo.Num() : -1, ExpectedEdges.Num());
	TestTrue(TEXT("the current output feeds the direct consumer"),
		Replacement && GateIn && Replacement->LinkedTo.Contains(GateIn));
	TestTrue(TEXT("the current output feeds the conversion consumer"),
		Replacement && ConvertIn && Replacement->LinkedTo.Contains(ConvertIn));
	TestTrue(TEXT("the current output never adopts the surviving input"),
		Replacement && RestoredInput && !Replacement->LinkedTo.Contains(RestoredInput));

	// The inventory the successful apply publishes is the reviewed consumer set, published completely.
	TestNotNull(TEXT("the successful same-name apply publishes its inventory"), Outcome.RewireInventory.Get());
	TArray<FString> PublishedEdges;
	TestTrue(TEXT("the successful same-name apply publishes the edge inventory"),
		PublishedEdgeEndpoints(Outcome.RewireInventory, PublishedEdges));
	TestEqual(TEXT("the published inventory is exactly the reviewed consumer set"),
		FString::Join(PublishedEdges, TEXT("\n")), FString::Join(ExpectedEdges, TEXT("\n")));
	TestTrue(TEXT("the published inventory claims completeness"),
		Outcome.RewireInventory.IsValid() && Outcome.RewireInventory->GetBoolField(TEXT("complete")));

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 17. An inventory the connected bridge could not publish whole is refused before approval
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireOversizedInventoryRefusedTest,
	"Cortex.Graph.Authoring.Migration.Rewire.RefusesOversizedCompleteInventory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireOversizedInventoryRefusedTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireOversized_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// A real orphan output with a few hundred real consumer inputs: no synthetic envelope is handed to
	// the boundary, the live edge inventory itself is past the caller's response budget.
	const int32 ExtraConsumers = 300;
	const int32 ExpectedConsumerCount = ExtraConsumers + 2;
	TestEqual(TEXT("the extra reviewed consumers are really linked to the orphan output"),
		AddExtraOrphanConsumers(Fixture, ExtraConsumers), ExtraConsumers);
	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle finds every reviewed consumer"), ExpectedEdges.Num(), ExpectedConsumerCount);

	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);
	const int32 NodesBefore = Fixture.Graph ? Fixture.Graph->Nodes.Num() : 0;
	const bool bDirtyBefore = Fixture.Package->IsDirty();

	// The published route exactly as a caller reaches it: the preview must refuse instead of publishing a
	// complete inventory the bridge would clip while keeping `complete: true`.
	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"), MakeShared<FCortexGraphCommandHandler>());
	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113114"));
	TestTrue(TEXT("the oversized preview request is built"), Request.IsValid());
	const FCortexCommandResult Result = Router.Execute(TEXT("graph.apply_patch"), Request);

	TestFalse(FString::Printf(TEXT("the oversized complete inventory is refused [%s: %s]"),
		*Result.ErrorCode, *Result.ErrorMessage), Result.bSuccess);
	TestEqual(TEXT("the oversized inventory refusal is LIMIT_EXCEEDED"),
		Result.ErrorCode, FString(CortexErrorCodes::LimitExceeded));
	TestTrue(FString::Printf(TEXT("the refusal names the bridge response limit [%s]"), *Result.ErrorMessage),
		Result.ErrorMessage.Contains(FString::Printf(TEXT("%d"), FCortexGraphMigrationOps::BridgeResponseCharLimit)));
	TestTrue(FString::Printf(TEXT("the refusal names the reviewed edge count [%s]"), *Result.ErrorMessage),
		Result.ErrorMessage.Contains(FString::Printf(TEXT("%d"), ExpectedConsumerCount)));

	// The native cause under the published outcome envelope: the measured size, the publishable bound and
	// completeness=false, so the refusal can never be mistaken for an approval set.
	const TSharedPtr<FJsonObject>* Cause = nullptr;
	const bool bHasCause = Result.ErrorDetails.IsValid()
		&& Result.ErrorDetails->TryGetObjectField(TEXT("error_details"), Cause) && Cause && Cause->IsValid();
	TestTrue(TEXT("the refusal carries the native cause"), bHasCause);
	if (bHasCause)
	{
		TestEqual(TEXT("the cause publishes the bridge response limit"),
			(*Cause)->GetIntegerField(TEXT("response_char_limit")), FCortexGraphMigrationOps::BridgeResponseCharLimit);
		TestEqual(TEXT("the cause publishes the publishable inventory bound"),
			(*Cause)->GetIntegerField(TEXT("publishable_inventory_chars")),
			FCortexGraphMigrationOps::MaxPublishableCallOutputInventoryChars);
		TestTrue(TEXT("the cause reports a measured inventory above the bound"),
			(*Cause)->GetIntegerField(TEXT("inventory_chars"))
				> FCortexGraphMigrationOps::MaxPublishableCallOutputInventoryChars);
		TestEqual(TEXT("the cause reports the reviewed edge count"),
			(*Cause)->GetIntegerField(TEXT("reviewed_edges")), ExpectedConsumerCount);
		TestFalse(TEXT("the cause never claims completeness"), (*Cause)->GetBoolField(TEXT("complete")));
	}

	// No clipped inventory is published in its place: no edge array, no completeness claim and no
	// approval state, so a caller reading the refusal cannot mistake a prefix for the reviewed set. The
	// compact outcome phases travel with it, so the refusal also states that no work ran.
	if (Result.ErrorDetails.IsValid())
	{
		FString ApplyStatus;
		FString CompileStatus;
		FString ReadbackStatus;
		FString SaveStatus;
		bool bReportedChanged = true;
		bool bReportedSaved = true;
		const bool bPhaseFields =
			Result.ErrorDetails->TryGetStringField(TEXT("apply_status"), ApplyStatus)
			&& Result.ErrorDetails->TryGetStringField(TEXT("compile_status"), CompileStatus)
			&& Result.ErrorDetails->TryGetStringField(TEXT("readback_status"), ReadbackStatus)
			&& Result.ErrorDetails->TryGetStringField(TEXT("save_status"), SaveStatus)
			&& Result.ErrorDetails->TryGetBoolField(TEXT("changed"), bReportedChanged)
			&& Result.ErrorDetails->TryGetBoolField(TEXT("saved"), bReportedSaved);
		TestTrue(TEXT("the refusal publishes the compact outcome phases"), bPhaseFields);
		TestEqual(TEXT("the refusal reports no apply"), ApplyStatus, FString(TEXT("not_requested")));
		TestEqual(TEXT("the refusal reports no compile"), CompileStatus, FString(TEXT("not_requested")));
		TestEqual(TEXT("the refusal reports no readback"), ReadbackStatus, FString(TEXT("not_requested")));
		TestEqual(TEXT("the refusal reports no save"), SaveStatus, FString(TEXT("not_requested")));
		TestFalse(TEXT("the refusal reports no change"), bReportedChanged);
		TestFalse(TEXT("the refusal reports no persistence"), bReportedSaved);
		TestFalse(TEXT("the refusal publishes no edge array"), Result.ErrorDetails->HasField(TEXT("edges")));
		TestFalse(TEXT("the refusal publishes no completeness claim"), Result.ErrorDetails->HasField(TEXT("complete")));
		TestFalse(TEXT("the refusal publishes no approval state"), Result.ErrorDetails->HasField(TEXT("awaiting_approval")));
	}

	// Fail closed before approval and before mutation: the asset is exactly the fixture again.
	TestEqual(TEXT("the refused preview changes no fingerprint"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("the refused preview leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);
	TestEqual(TEXT("the refused preview leaves the node count"),
		Fixture.Graph ? Fixture.Graph->Nodes.Num() : -1, NodesBefore);
	TestEqual(TEXT("the refused preview leaves the dirty baseline"), Fixture.Package->IsDirty(), bDirtyBefore);
	UEdGraphPin* const OrphanAfter = Fixture.FindHistoricOutputPin();
	TestNotNull(TEXT("the orphan output pin survives the refusal"), OrphanAfter);
	TestEqual(TEXT("the orphan output keeps every reviewed consumer"),
		OrphanAfter ? OrphanAfter->LinkedTo.Num() : -1, ExpectedConsumerCount);

	Fixture.Cleanup();
	return true;
}

// ---------------------------------------------------------------------------
// 18. A large but publishable inventory stays actionable and fills the response budget whole
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphMigrationRewireCompleteInventoryWithinBudgetTest,
	"Cortex.Graph.Authoring.Migration.Rewire.PublishesCompleteInventoryWithinBridgeBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphMigrationRewireCompleteInventoryWithinBudgetTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using namespace CortexGraphMigrationRewireTest;
	ResetSeams();
	ExpectFixtureCompileError(*this);

	FFixture Fixture;
	TestTrue(TEXT("rewire fixture is created"), Fixture.Build(TEXT("WBP_MigrationRewireWithinBudget_T113")));
	if (!Fixture.Blueprint) { Fixture.Cleanup(); return false; }

	// Close to the budget without crossing it: the boundary must refuse only what the bridge could not
	// publish, never a large inventory that still fits.
	const int32 ExtraConsumers = 90;
	const int32 ExpectedConsumerCount = ExtraConsumers + 2;
	TestEqual(TEXT("the extra reviewed consumers are really linked to the orphan output"),
		AddExtraOrphanConsumers(Fixture, ExtraConsumers), ExtraConsumers);
	const TArray<FString> ExpectedEdges = Fixture.OrphanEdgeEndpoints();
	TestEqual(TEXT("the fixture oracle finds every reviewed consumer"), ExpectedEdges.Num(), ExpectedConsumerCount);

	const FString HashBefore = LiveGraphHash(Fixture.Blueprint);
	const FString GraphBefore = CaptureRewireGraph(Fixture.Graph);

	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"), MakeShared<FCortexGraphCommandHandler>());
	TSharedPtr<FJsonObject> Request = RewireRequest(Fixture, TEXT("00000000-0000-0000-0000-000000113115"));
	TestTrue(TEXT("the large preview request is built"), Request.IsValid());
	const FCortexCommandResult Result = Router.Execute(TEXT("graph.apply_patch"), Request);

	TestTrue(FString::Printf(TEXT("the large complete inventory previews [%s: %s]"),
		*Result.ErrorCode, *Result.ErrorMessage), Result.bSuccess);
	if (Result.bSuccess && Result.Data.IsValid())
	{
		TestTrue(TEXT("the large preview carries the apply token"), Result.Data->HasField(TEXT("validation_hash")));
		TestTrue(TEXT("the large preview publishes its inventory for approval"),
			Result.Data->GetBoolField(TEXT("awaiting_approval")));
		TArray<FString> PublishedEdges;
		TestTrue(TEXT("the large preview publishes the edge inventory"),
			PublishedEdgeEndpoints(Result.Data, PublishedEdges));
		TestEqual(TEXT("the published inventory is exactly the reviewed consumer set"),
			FString::Join(PublishedEdges, TEXT("\n")), FString::Join(ExpectedEdges, TEXT("\n")));

		// The whole response the caller receives — not only its edge array — is measured with the bridge's
		// own encoding rule, so the accepted boundary is proved to fit the bridge budget instead of assumed.
		int32 EncodedChars = 0;
		TestTrue(TEXT("the published preview response is measurable"),
			FCortexGraphMigrationOps::EncodedResponseChars(Result.Data, EncodedChars));
		TestTrue(FString::Printf(TEXT("the whole published preview fits the bridge budget (%d of %d)"),
			EncodedChars, FCortexGraphMigrationOps::BridgeResponseCharLimit),
			EncodedChars > 0 && EncodedChars <= FCortexGraphMigrationOps::BridgeResponseCharLimit);
	}

	TestEqual(TEXT("the large preview mutates nothing"), LiveGraphHash(Fixture.Blueprint), HashBefore);
	TestEqual(TEXT("the large preview leaves every node, pin, flag and link"),
		CaptureRewireGraph(Fixture.Graph), GraphBefore);

	Fixture.Cleanup();
	return true;
}
#endif // WITH_EDITOR && WITH_AUTOMATION_TESTS
