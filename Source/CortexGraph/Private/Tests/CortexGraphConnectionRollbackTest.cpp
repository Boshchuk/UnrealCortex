// CortexGraph connection rollback / failure-atomicity tests.
//
// Covers the three connection shapes the rollback journal must handle honestly:
//   1. Direct connection (CONNECT_RESPONSE_MAKE)   -> rollback breaks the link.
//   2. Replacement (CONNECT_RESPONSE_BREAK_OTHERS) -> rejected inside rollback-enabled
//      batches because the journal cannot restore the displaced link; allowed when the
//      batch does not roll back.
//   3. Conversion/promotion (CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE) -> rejected
//      inside rollback-enabled batches because the journal cannot remove intermediate
//      nodes; the connection would otherwise be verified "clean" while leaving
//      replacement/conversion topology behind.

#include "Misc/AutomationTest.h"
#include "CortexBatchScope.h"
#include "CortexCommandRouter.h"
#include "CortexGraphCommandHandler.h"
#include "Dom/JsonObject.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "GameFramework/Actor.h"
#include "K2Node_IfThenElse.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphConnectionRollbackTest,
	"Cortex.Graph.ConnectionRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
UEdGraphNode* FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node != nullptr && Node->GetName() == NodeId)
		{
			return Node;
		}
	}
	return nullptr;
}

UEdGraphPin* FindPinById(UEdGraph* Graph, const FString& NodeId, const FString& PinName)
{
	UEdGraphNode* Node = FindNodeById(Graph, NodeId);
	return Node ? Node->FindPin(FName(*PinName)) : nullptr;
}

bool AreLinked(UEdGraph* Graph, const FString& SourceNode, const FString& SourcePin,
	const FString& TargetNode, const FString& TargetPin)
{
	if (Graph == nullptr)
	{
		return false;
	}
	UEdGraphPin* Source = FindPinById(Graph, SourceNode, SourcePin);
	UEdGraphPin* Target = FindPinById(Graph, TargetNode, TargetPin);
	return Source != nullptr && Target != nullptr && Source->LinkedTo.Contains(Target);
}

const TSharedPtr<FJsonObject>* FindBatchStepResult(const TSharedPtr<FJsonObject>& ErrorDetails, int32 StepIndex)
{
	if (!ErrorDetails.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
	if (!ErrorDetails->TryGetArrayField(TEXT("results"), Results) || Results == nullptr)
	{
		return nullptr;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Results)
	{
		const TSharedPtr<FJsonObject>* Entry = nullptr;
		if (Value.IsValid() && Value->TryGetObject(Entry) && Entry != nullptr && (*Entry).IsValid())
		{
			int32 Index = -1;
			if ((*Entry)->TryGetNumberField(TEXT("index"), Index) && Index == StepIndex)
			{
				return Entry;
			}
		}
	}
	return nullptr;
}
}

bool FCortexGraphConnectionRollbackTest::RunTest(const FString& Parameters)
{
	UPackage* TestPackage = CreatePackage(TEXT("/Game/Temp/CortexGraphConnectionRollbackTest"));
	TestPackage->SetPackageFlags(PKG_PlayInEditor);
	UBlueprint* TestBP = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), TestPackage, TEXT("BP_ConnectionRollbackTest"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	TestNotNull(TEXT("test blueprint created"), TestBP);
	if (TestBP == nullptr)
	{
		return false;
	}
	const FString AssetPath = TestBP->GetPathName();
	UEdGraph* EventGraph = TestBP->UbergraphPages.Num() > 0 ? TestBP->UbergraphPages[0] : nullptr;
	TestNotNull(TEXT("event graph present"), EventGraph);

	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"),
		MakeShared<FCortexGraphCommandHandler>());

	auto AddNode = [&Router, &AssetPath, this](const FString& NodeClass, const TSharedPtr<FJsonObject>& NodeParams,
		FString& OutNodeId) -> bool
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("asset_path"), AssetPath);
		Params->SetStringField(TEXT("node_class"), NodeClass);
		if (NodeParams.IsValid())
		{
			Params->SetObjectField(TEXT("params"), NodeParams);
		}
		FCortexCommandResult Result = Router.Execute(TEXT("graph.add_node"), Params);
		TestTrue(FString::Printf(TEXT("add node %s succeeds"), *NodeClass), Result.bSuccess);
		if (!Result.bSuccess)
		{
			AddError(FString::Printf(TEXT("add_node %s failed: %s (%s)"), *NodeClass, *Result.ErrorMessage, *Result.ErrorCode));
		}
		if (Result.bSuccess && Result.Data.IsValid())
		{
			Result.Data->TryGetStringField(TEXT("node_id"), OutNodeId);
			return true;
		}
		return false;
	};

	auto Connect = [&Router, &AssetPath](const FString& SourceNode, const FString& SourcePin,
		const FString& TargetNode, const FString& TargetPin) -> FCortexCommandResult
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("asset_path"), AssetPath);
		Params->SetStringField(TEXT("source_node"), SourceNode);
		Params->SetStringField(TEXT("source_pin"), SourcePin);
		Params->SetStringField(TEXT("target_node"), TargetNode);
		Params->SetStringField(TEXT("target_pin"), TargetPin);
		return Router.Execute(TEXT("graph.connect"), Params);
	};

	auto CountNodes = [&Router, &AssetPath](int32& OutCount) -> bool
	{
		TSharedPtr<FJsonObject> ListParams = MakeShared<FJsonObject>();
		ListParams->SetStringField(TEXT("asset_path"), AssetPath);
		FCortexCommandResult List = Router.Execute(TEXT("graph.get_subgraph"), ListParams);
		if (!List.bSuccess || !List.Data.IsValid())
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!List.Data->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr)
		{
			return false;
		}
		OutCount = Nodes->Num();
		return true;
	};

	auto MakeBatch = [](const TArray<TSharedPtr<FJsonValue>>& Commands, bool bRollback) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Batch = MakeShared<FJsonObject>();
		Batch->SetArrayField(TEXT("commands"), Commands);
		Batch->SetBoolField(TEXT("stop_on_error"), true);
		if (bRollback)
		{
			Batch->SetBoolField(TEXT("rollback_on_error"), true);
			Batch->SetBoolField(TEXT("verify_rollback"), true);
		}
		return Batch;
	};

	auto Step = [](const FString& Command, const TSharedPtr<FJsonObject>& StepParams) -> TSharedPtr<FJsonValue>
	{
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("command"), Command);
		if (StepParams.IsValid())
		{
			Entry->SetObjectField(TEXT("params"), StepParams);
		}
		return MakeShared<FJsonValueObject>(Entry);
	};

	auto ConnectParams = [&AssetPath](const FString& SourceNode, const FString& SourcePin,
		const FString& TargetNode, const FString& TargetPin) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("asset_path"), AssetPath);
		Params->SetStringField(TEXT("source_node"), SourceNode);
		Params->SetStringField(TEXT("source_pin"), SourcePin);
		Params->SetStringField(TEXT("target_node"), TargetNode);
		Params->SetStringField(TEXT("target_pin"), TargetPin);
		return Params;
	};

	// ── 1. Direct connection rollback ─────────────────────────────────────────────
	int32 BaselineCount = -1;
	TestTrue(TEXT("baseline node count read"), CountNodes(BaselineCount));

	FString NodeA;
	FString NodeB;
	{
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, NodeA);
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, NodeB);

		// Failing step after the connection (pre-mutation validation failure).
		TSharedPtr<FJsonObject> BadFunction = MakeShared<FJsonObject>();
		BadFunction->SetStringField(TEXT("function_name"), TEXT("Missing.Owner"));
		TSharedPtr<FJsonObject> BadParams = MakeShared<FJsonObject>();
		BadParams->SetStringField(TEXT("asset_path"), AssetPath);
		BadParams->SetStringField(TEXT("node_class"), TEXT("UK2Node_CallFunction"));
		BadParams->SetObjectField(TEXT("params"), BadFunction);

		TArray<TSharedPtr<FJsonValue>> Commands;
		Commands.Add(Step(TEXT("graph.connect"), ConnectParams(NodeA, TEXT("then"), NodeB, TEXT("execute"))));
		Commands.Add(Step(TEXT("graph.add_node"), BadParams));

		FCortexCommandResult Result = Router.Execute(TEXT("batch"), MakeBatch(Commands, true));
		TestFalse(TEXT("batch with failing step fails"), Result.bSuccess);
		if (!Result.bSuccess && Result.ErrorDetails.IsValid())
		{
			const TSharedPtr<FJsonObject>* RollbackObj = nullptr;
			TestTrue(TEXT("rollback object present"),
				Result.ErrorDetails->TryGetObjectField(TEXT("rollback"), RollbackObj) && RollbackObj != nullptr);
			if (RollbackObj != nullptr)
			{
				TestTrue(TEXT("direct connect rollback verified"), (*RollbackObj)->GetBoolField(TEXT("verified")));
			}
			const TArray<TSharedPtr<FJsonValue>>* Residual = nullptr;
			if (Result.ErrorDetails->TryGetArrayField(TEXT("residual_changes"), Residual))
			{
				TestEqual(TEXT("no residual graph changes"), Residual->Num(), 0);
			}
		}
		TestTrue(TEXT("direct link broken by rollback"),
			!AreLinked(EventGraph, NodeA, TEXT("then"), NodeB, TEXT("execute")));

		int32 After = -1;
		TestTrue(TEXT("post-rollback node count read"), CountNodes(After));
		TestEqual(TEXT("node count preserved (only the link was rolled back)"), After, BaselineCount + 2);
	}

	// ── 2. Replacement is rejected inside a rollback-enabled batch ────────────────
	FString NodeC;
	FString NodeD;
	FString NodeE;
	{
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, NodeC);
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, NodeD);
		TestTrue(TEXT("replacement precondition connect succeeds"),
			Connect(NodeC, TEXT("then"), NodeD, TEXT("execute")).bSuccess);

		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, NodeE);

		TArray<TSharedPtr<FJsonValue>> Commands;
		Commands.Add(Step(TEXT("graph.connect"), ConnectParams(NodeC, TEXT("then"), NodeE, TEXT("execute"))));

		FCortexCommandResult Result = Router.Execute(TEXT("batch"), MakeBatch(Commands, true));
		TestFalse(TEXT("replacement connect fails in rollback batch"), Result.bSuccess);
		const TSharedPtr<FJsonObject>* StepEntry = FindBatchStepResult(Result.ErrorDetails, 0);
		TestNotNull(TEXT("batch step 0 result present"), StepEntry);
		if (StepEntry != nullptr)
		{
			TestEqual(TEXT("replacement rejected as INVALID_OPERATION"),
				(*StepEntry)->GetStringField(TEXT("error_code")), CortexErrorCodes::InvalidOperation);
		}
		TestTrue(TEXT("displaced link preserved after rejection"),
			AreLinked(EventGraph, NodeC, TEXT("then"), NodeD, TEXT("execute")));

		int32 After = -1;
		TestTrue(TEXT("post-rejection node count read"), CountNodes(After));
		TestEqual(TEXT("node count unchanged by rejected replacement"), After, BaselineCount + 5);
	}

	// ── 3. Non-rollback batch still allows replacement (schema default) ───────────
	{
		FString NodeF;
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, NodeF);

		TArray<TSharedPtr<FJsonValue>> Commands;
		Commands.Add(Step(TEXT("graph.connect"), ConnectParams(NodeC, TEXT("then"), NodeF, TEXT("execute"))));
		FCortexCommandResult Result = Router.Execute(TEXT("batch"), MakeBatch(Commands, false));
		TestTrue(TEXT("replacement allowed in non-rollback batch"), Result.bSuccess);
		TestTrue(TEXT("replacement link established"),
			AreLinked(EventGraph, NodeC, TEXT("then"), NodeF, TEXT("execute")));

		int32 After = -1;
		TestTrue(TEXT("post-replacement node count read"), CountNodes(After));
		TestEqual(TEXT("node count unchanged by replacement"), After, BaselineCount + 6);
	}

	// ── 4. Conversion is rejected inside a rollback-enabled batch ─────────────────
	{
		FString ConvNode;
		FString PrintNode;
		TSharedPtr<FJsonObject> NameFunction = MakeShared<FJsonObject>();
		NameFunction->SetStringField(TEXT("function_name"), TEXT("KismetSystemLibrary.MakeLiteralName"));
		AddNode(TEXT("UK2Node_CallFunction"), NameFunction, ConvNode);
		TSharedPtr<FJsonObject> PrintFunction = MakeShared<FJsonObject>();
		PrintFunction->SetStringField(TEXT("function_name"), TEXT("KismetSystemLibrary.PrintString"));
		AddNode(TEXT("UK2Node_CallFunction"), PrintFunction, PrintNode);
		const int32 PreBatchCount = BaselineCount + 8;

		// Name output -> String input requires the Conv_NameToString autocast
		// (BlueprintAutocast), so the schema answers MAKE_WITH_CONVERSION_NODE.
		TArray<TSharedPtr<FJsonValue>> Commands;
		Commands.Add(Step(TEXT("graph.connect"), ConnectParams(ConvNode, TEXT("ReturnValue"), PrintNode, TEXT("InString"))));

		FCortexCommandResult Result = Router.Execute(TEXT("batch"), MakeBatch(Commands, true));
		TestFalse(TEXT("conversion connect fails in rollback batch"), Result.bSuccess);
		const TSharedPtr<FJsonObject>* StepEntry = FindBatchStepResult(Result.ErrorDetails, 0);
		TestNotNull(TEXT("conversion batch step result present"), StepEntry);
		if (StepEntry != nullptr)
		{
			TestEqual(TEXT("conversion rejected as INVALID_OPERATION"),
				(*StepEntry)->GetStringField(TEXT("error_code")), CortexErrorCodes::InvalidOperation);
		}
		int32 After = -1;
		TestTrue(TEXT("post-conversion-rejection node count read"), CountNodes(After));
		TestEqual(TEXT("node count restored after rejected conversion"), After, PreBatchCount);
	}

	// ── 5. Reconstructed pins are re-resolved by stable node/pin identifiers ──────
	{
		FString SourceId;
		FString TargetId;
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, SourceId);
		AddNode(TEXT("UK2Node_IfThenElse"), nullptr, TargetId);
		UEdGraphNode* OriginalSource = FindNodeById(EventGraph, SourceId);
		UEdGraphNode* OriginalTarget = FindNodeById(EventGraph, TargetId);
		TestNotNull(TEXT("reconstruction source exists"), OriginalSource);
		TestNotNull(TEXT("reconstruction target exists"), OriginalTarget);

		FCortexBatchScope ManualBatchScope;
		FCortexBatchScope::SetRollbackEnabled(true);
		FCortexCommandResult Connected = Connect(SourceId, TEXT("then"), TargetId, TEXT("execute"));
		TestTrue(TEXT("manual-batch connection succeeds"), Connected.bSuccess);

		if (OriginalSource != nullptr && OriginalTarget != nullptr)
		{
			const FGuid SourceGuid = OriginalSource->NodeGuid;
			const FGuid TargetGuid = OriginalTarget->NodeGuid;
			EventGraph->RemoveNode(OriginalSource);
			EventGraph->RemoveNode(OriginalTarget);
			OriginalSource->Rename(*FString::Printf(TEXT("%s_Replaced"), *SourceId),
				GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			OriginalTarget->Rename(*FString::Printf(TEXT("%s_Replaced"), *TargetId),
				GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);

			UK2Node_IfThenElse* ReplacementSource = NewObject<UK2Node_IfThenElse>(EventGraph, FName(*SourceId));
			ReplacementSource->NodeGuid = SourceGuid;
			ReplacementSource->AllocateDefaultPins();
			EventGraph->AddNode(ReplacementSource, false, false);
			UK2Node_IfThenElse* ReplacementTarget = NewObject<UK2Node_IfThenElse>(EventGraph, FName(*TargetId));
			ReplacementTarget->NodeGuid = TargetGuid;
			ReplacementTarget->AllocateDefaultPins();
			EventGraph->AddNode(ReplacementTarget, false, false);
			ReplacementSource->FindPin(TEXT("then"))->MakeLinkTo(ReplacementTarget->FindPin(TEXT("execute")));

			const FCortexBatchRollbackResult ReconstructedRollback = FCortexBatchScope::ExecuteRollback();
			TestTrue(TEXT("connection rollback verifies after pin reconstruction"), ReconstructedRollback.bVerified);
			TestFalse(TEXT("reconstructed logical connection removed"),
				AreLinked(EventGraph, SourceId, TEXT("then"), TargetId, TEXT("execute")));
		}
	}

	TestBP->MarkAsGarbage();
	TestPackage->MarkAsGarbage();
	return true;
}
