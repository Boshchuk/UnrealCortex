#include "Misc/AutomationTest.h"
#include "CortexBatchScope.h"
#include "CortexCommandRouter.h"
#include "CortexGraphCommandHandler.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_IfThenElse.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphBatchRollbackTest,
	"Cortex.Graph.Batch.Rollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphBatchRollbackTest::RunTest(const FString& Parameters)
{
	UPackage* TestPackage = CreatePackage(TEXT("/Game/Temp/CortexGraphBatchRollbackTest"));
	TestPackage->SetPackageFlags(PKG_PlayInEditor);
	UBlueprint* TestBP = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), TestPackage, TEXT("BP_RollbackTest"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	TestNotNull(TEXT("test blueprint created"), TestBP);
	if (TestBP == nullptr) return false;
	const FString AssetPath = TestBP->GetPathName();
	UEdGraph* EventGraph = TestBP->UbergraphPages.Num() > 0 ? TestBP->UbergraphPages[0] : nullptr;
	TestNotNull(TEXT("event graph present"), EventGraph);

	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"),
		MakeShared<FCortexGraphCommandHandler>());

	auto CountNodes = [&Router, &AssetPath](int32& OutCount) -> bool
	{
		TSharedPtr<FJsonObject> ListParams = MakeShared<FJsonObject>();
		ListParams->SetStringField(TEXT("asset_path"), AssetPath);
		FCortexCommandResult List = Router.Execute(TEXT("graph.get_subgraph"), ListParams);
		if (!List.bSuccess || !List.Data.IsValid()) return false;
		const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
		if (!List.Data->TryGetArrayField(TEXT("nodes"), Nodes) || Nodes == nullptr) return false;
		OutCount = Nodes->Num();
		return true;
	};

	int32 Before = -1;
	TestTrue(TEXT("initial node count read"), CountNodes(Before));

	TSharedRef<FJsonObject> Step0 = MakeShared<FJsonObject>();
	Step0->SetStringField(TEXT("command"), TEXT("graph.add_node"));
	TSharedRef<FJsonObject> Step0Params = MakeShared<FJsonObject>();
	Step0Params->SetStringField(TEXT("asset_path"), AssetPath);
	Step0Params->SetStringField(TEXT("node_class"), TEXT("UK2Node_IfThenElse"));
	Step0->SetObjectField(TEXT("params"), Step0Params);

	TSharedRef<FJsonObject> Step1 = MakeShared<FJsonObject>();
	Step1->SetStringField(TEXT("command"), TEXT("graph.add_node"));
	TSharedRef<FJsonObject> Step1Params = MakeShared<FJsonObject>();
	Step1Params->SetStringField(TEXT("asset_path"), AssetPath);
	Step1Params->SetStringField(TEXT("node_class"), TEXT("UK2Node_CallFunction"));
	TSharedRef<FJsonObject> BadFunction = MakeShared<FJsonObject>();
	BadFunction->SetStringField(TEXT("function_name"), TEXT("Missing.Owner"));
	Step1Params->SetObjectField(TEXT("params"), BadFunction);
	Step1->SetObjectField(TEXT("params"), Step1Params);

	TSharedPtr<FJsonObject> Batch = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Commands;
	Commands.Add(MakeShared<FJsonValueObject>(Step0));
	Commands.Add(MakeShared<FJsonValueObject>(Step1));
	Batch->SetArrayField(TEXT("commands"), Commands);
	Batch->SetBoolField(TEXT("stop_on_error"), true);
	Batch->SetBoolField(TEXT("rollback_on_error"), true);
	Batch->SetBoolField(TEXT("verify_rollback"), true);

	FCortexCommandResult Result = Router.Execute(TEXT("batch"), Batch);
	TestFalse(TEXT("batch fails on second step"), Result.bSuccess);
	if (Result.ErrorDetails.IsValid())
	{
		bool bRollbackVerified = false;
		const TSharedPtr<FJsonObject>* RollbackObj = nullptr;
		if (Result.ErrorDetails->TryGetObjectField(TEXT("rollback"), RollbackObj) && RollbackObj != nullptr)
		{
			bRollbackVerified = (*RollbackObj)->GetBoolField(TEXT("verified"));
		}
		TestTrue(TEXT("rollback verified"), bRollbackVerified);
		const TArray<TSharedPtr<FJsonValue>>* Residual = nullptr;
		if (Result.ErrorDetails->TryGetArrayField(TEXT("residual_changes"), Residual))
		{
			TestEqual(TEXT("no residual graph changes"), Residual->Num(), 0);
		}
	}

	int32 After = -1;
	TestTrue(TEXT("post-rollback node count read"), CountNodes(After));
	TestEqual(TEXT("node count restored to pre-batch value"), After, Before);

	// Composite placement creates both a graph node and a BoundGraph entry in EventGraph.SubGraphs.
	// Rollback must destroy both pieces instead of verifying only that the parent node disappeared.
	if (EventGraph != nullptr)
	{
		const int32 SubgraphsBefore = EventGraph->SubGraphs.Num();
		TSharedRef<FJsonObject> CompositeStep = MakeShared<FJsonObject>();
		CompositeStep->SetStringField(TEXT("command"), TEXT("graph.add_node"));
		TSharedRef<FJsonObject> CompositeParams = MakeShared<FJsonObject>();
		CompositeParams->SetStringField(TEXT("asset_path"), AssetPath);
		CompositeParams->SetStringField(TEXT("node_class"), TEXT("UK2Node_Composite"));
		CompositeStep->SetObjectField(TEXT("params"), CompositeParams);
		TSharedPtr<FJsonObject> CompositeBatch = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> CompositeCommands;
		CompositeCommands.Add(MakeShared<FJsonValueObject>(CompositeStep));
		CompositeCommands.Add(MakeShared<FJsonValueObject>(Step1));
		CompositeBatch->SetArrayField(TEXT("commands"), CompositeCommands);
		CompositeBatch->SetBoolField(TEXT("stop_on_error"), true);
		CompositeBatch->SetBoolField(TEXT("rollback_on_error"), true);
		CompositeBatch->SetBoolField(TEXT("verify_rollback"), true);
		FCortexCommandResult CompositeResult = Router.Execute(TEXT("batch"), CompositeBatch);
		TestFalse(TEXT("composite batch fails on invalid second step"), CompositeResult.bSuccess);
		TestEqual(TEXT("composite rollback removes generated bound graph"),
			EventGraph->SubGraphs.Num(), SubgraphsBefore);
	}

	// Journal callbacks must re-resolve a logically identical node after reconstruction rather than
	// retaining the original UObject pointer. Simulate reconstruction by replacing the added node
	// with a new object that has the same stable name and GUID before rollback executes.
	TestNotNull(TEXT("event graph present for reconstruction rollback"), EventGraph);
	if (EventGraph != nullptr)
	{
		FCortexBatchScope ManualBatchScope;
		FCortexBatchScope::SetRollbackEnabled(true);
		FCortexCommandResult Added = Router.Execute(TEXT("graph.add_node"), Step0Params);
		TestTrue(TEXT("manual-batch add succeeds"), Added.bSuccess);
		const FString AddedNodeId = Added.bSuccess && Added.Data.IsValid()
			? Added.Data->GetStringField(TEXT("node_id")) : FString();
		UEdGraphNode* OriginalNode = nullptr;
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			if (Node != nullptr && Node->GetName() == AddedNodeId)
			{
				OriginalNode = Node;
				break;
			}
		}
		TestNotNull(TEXT("original journaled node found"), OriginalNode);
		if (OriginalNode != nullptr)
		{
			const FGuid StableGuid = OriginalNode->NodeGuid;
			EventGraph->RemoveNode(OriginalNode);
			OriginalNode->Rename(
				*FString::Printf(TEXT("%s_Replaced"), *AddedNodeId),
				GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			UK2Node_IfThenElse* Replacement = NewObject<UK2Node_IfThenElse>(EventGraph, FName(*AddedNodeId));
			Replacement->NodeGuid = StableGuid;
			Replacement->AllocateDefaultPins();
			EventGraph->AddNode(Replacement, false, false);

			const FCortexBatchRollbackResult ReconstructedRollback = FCortexBatchScope::ExecuteRollback();
			TestTrue(TEXT("rollback verifies after node reconstruction"), ReconstructedRollback.bVerified);
			TestNull(TEXT("reconstructed logical node removed"),
				EventGraph->Nodes.FindByPredicate([&AddedNodeId](const UEdGraphNode* Node)
				{
					return Node != nullptr && Node->GetName() == AddedNodeId;
				})
					? *EventGraph->Nodes.FindByPredicate([&AddedNodeId](const UEdGraphNode* Node)
					{
						return Node != nullptr && Node->GetName() == AddedNodeId;
					})
					: nullptr);
		}
	}

	TestBP->MarkAsGarbage();
	TestPackage->MarkAsGarbage();
	return true;
}
