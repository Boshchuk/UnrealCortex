#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "CortexCommandRouter.h"
#include "CortexGraphCommandHandler.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_Composite.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameFramework/Actor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexGraphRemoveNodeTest,
    "Cortex.Graph.RemoveNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexGraphRemoveNodeTest::RunTest(const FString& Parameters)
{
    UBlueprint* TestBP = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(),
        GetTransientPackage(),
        FName(TEXT("BP_CortexGraphTest_RemoveNode")),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass()
    );
    TestNotNull(TEXT("Test Blueprint should be created"), TestBP);

    if (TestBP == nullptr)
    {
        return true;
    }

    FString AssetPath = TestBP->GetPathName();
    UEdGraph* EventGraph = TestBP->UbergraphPages.Num() > 0 ? TestBP->UbergraphPages[0] : nullptr;
    TestNotNull(TEXT("EventGraph should exist"), EventGraph);

    FCortexCommandRouter Router;
    Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.0"),
        MakeShared<FCortexGraphCommandHandler>());

    // Add two nodes and connect them
    FString Node1Id;
    FString Node2Id;

    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), AssetPath);
        Params->SetStringField(TEXT("node_class"), TEXT("UK2Node_CallFunction"));
        TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
        NodeParams->SetStringField(TEXT("function_name"), TEXT("KismetSystemLibrary.PrintString"));
        Params->SetObjectField(TEXT("params"), NodeParams);
        FCortexCommandResult Result = Router.Execute(TEXT("graph.add_node"), Params);
        TestTrue(TEXT("add first node should succeed"), Result.bSuccess);
        if (Result.Data.IsValid())
        {
            Result.Data->TryGetStringField(TEXT("node_id"), Node1Id);
        }
    }

    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), AssetPath);
        Params->SetStringField(TEXT("node_class"), TEXT("UK2Node_CallFunction"));
        TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
        NodeParams->SetStringField(TEXT("function_name"), TEXT("KismetSystemLibrary.PrintString"));
        Params->SetObjectField(TEXT("params"), NodeParams);
        FCortexCommandResult Result = Router.Execute(TEXT("graph.add_node"), Params);
        TestTrue(TEXT("add second node should succeed"), Result.bSuccess);
        if (Result.Data.IsValid())
        {
            Result.Data->TryGetStringField(TEXT("node_id"), Node2Id);
        }
    }

    // Connect them
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), AssetPath);
        Params->SetStringField(TEXT("source_node"), Node1Id);
        Params->SetStringField(TEXT("source_pin"), TEXT("then"));
        Params->SetStringField(TEXT("target_node"), Node2Id);
        Params->SetStringField(TEXT("target_pin"), TEXT("execute"));
        Router.Execute(TEXT("graph.connect"), Params);
    }

    int32 NodeCountBeforeRemove = EventGraph != nullptr ? EventGraph->Nodes.Num() : 0;

    // Test: remove the first node
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), AssetPath);
        Params->SetStringField(TEXT("node_id"), Node1Id);

        FCortexCommandResult Result = Router.Execute(TEXT("graph.remove_node"), Params);
        TestTrue(TEXT("remove_node should succeed"), Result.bSuccess);

        if (Result.Data.IsValid())
        {
            FString RemovedId;
            Result.Data->TryGetStringField(TEXT("removed_node_id"), RemovedId);
            TestEqual(TEXT("Removed node ID should match"), RemovedId, Node1Id);

            double DisconnectedPins = 0;
            Result.Data->TryGetNumberField(TEXT("disconnected_pins"), DisconnectedPins);
            TestTrue(TEXT("Should have disconnected at least 1 pin"), DisconnectedPins >= 1);
        }

        // Verify node count decreased
        if (EventGraph != nullptr)
        {
            TestEqual(TEXT("Graph should have one fewer node"), EventGraph->Nodes.Num(), NodeCountBeforeRemove - 1);
        }
    }

    // Verify Node2's execute pin is now disconnected
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), AssetPath);
        Params->SetStringField(TEXT("node_id"), Node2Id);

        FCortexCommandResult Result = Router.Execute(TEXT("graph.get_node"), Params);
        TestTrue(TEXT("get_node on Node2 should succeed"), Result.bSuccess);

        if (Result.Data.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* PinsArray = nullptr;
            Result.Data->TryGetArrayField(TEXT("pins"), PinsArray);
            if (PinsArray != nullptr)
            {
                for (const TSharedPtr<FJsonValue>& PinVal : *PinsArray)
                {
                    const TSharedPtr<FJsonObject>* PinObj = nullptr;
                    if (PinVal->TryGetObject(PinObj))
                    {
                        FString PinName;
                        (*PinObj)->TryGetStringField(TEXT("name"), PinName);
                        if (PinName == TEXT("execute"))
                        {
                            bool bIsConnected = false;
                            (*PinObj)->TryGetBoolField(TEXT("is_connected"), bIsConnected);
                            TestFalse(TEXT("Node2 execute pin should be disconnected after Node1 removed"), bIsConnected);
                            break;
                        }
                    }
                }
            }
        }
    }

    // Test: remove non-existent node
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        Params->SetStringField(TEXT("asset_path"), AssetPath);
        Params->SetStringField(TEXT("node_id"), TEXT("NonExistentNode_999"));

        FCortexCommandResult Result = Router.Execute(TEXT("graph.remove_node"), Params);
        TestFalse(TEXT("remove non-existent node should fail"), Result.bSuccess);
        TestEqual(TEXT("Error should be NODE_NOT_FOUND"), Result.ErrorCode, CortexErrorCodes::NodeNotFound);
    }

    // Cleanup
    TestBP->MarkAsGarbage();

    return true;
}

// ── Remove composite node cleans up BoundGraph ──────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexGraphRemoveCompositeNodeTest,
    "Cortex.Graph.RemoveNode.CompositeCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexGraphRemoveCompositeNodeTest::RunTest(const FString& Parameters)
{
    UBlueprint* TestBP = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(),
        GetTransientPackage(),
        FName(*FString::Printf(TEXT("BP_CortexGraphTest_RemoveComposite_%s"), *FGuid::NewGuid().ToString())),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass()
    );
    TestNotNull(TEXT("Test Blueprint should be created"), TestBP);
    if (TestBP == nullptr)
    {
        return true;
    }

    FString AssetPath = TestBP->GetPathName();
    UEdGraph* EventGraph = TestBP->UbergraphPages.Num() > 0 ? TestBP->UbergraphPages[0] : nullptr;
    TestNotNull(TEXT("EventGraph should exist"), EventGraph);

    // Create a composite node with a BoundGraph
    UK2Node_Composite* CompositeNode = NewObject<UK2Node_Composite>(EventGraph);
    CompositeNode->CreateNewGuid();
    EventGraph->AddNode(CompositeNode, true, false);
    CompositeNode->PostPlacedNewNode();
    CompositeNode->AllocateDefaultPins();

    FString CompositeNodeId = CompositeNode->GetName();
    UEdGraph* BoundGraph = CompositeNode->BoundGraph;
    TestNotNull(TEXT("Composite should have a BoundGraph"), BoundGraph);

    // Verify the BoundGraph is in SubGraphs
    bool bInSubGraphs = EventGraph->SubGraphs.Contains(BoundGraph);
    TestTrue(TEXT("BoundGraph should be in SubGraphs"), bInSubGraphs);

    int32 NodeCountBefore = EventGraph->Nodes.Num();
    int32 SubGraphCountBefore = EventGraph->SubGraphs.Num();

    // Remove the composite node via the command router
    FCortexCommandRouter Router;
    Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.0"),
        MakeShared<FCortexGraphCommandHandler>());

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), AssetPath);
    Params->SetStringField(TEXT("node_id"), CompositeNodeId);

    FCortexCommandResult Result = Router.Execute(TEXT("graph.remove_node"), Params);
    TestTrue(TEXT("remove_node on composite should succeed"), Result.bSuccess);

    // Verify the composite node was removed
    TestEqual(TEXT("EventGraph should have one fewer node"), EventGraph->Nodes.Num(), NodeCountBefore - 1);

    // Verify the BoundGraph was cleaned up from SubGraphs
    TestEqual(TEXT("SubGraphs should have one fewer entry"), EventGraph->SubGraphs.Num(), SubGraphCountBefore - 1);
    TestFalse(TEXT("BoundGraph should no longer be in SubGraphs"), EventGraph->SubGraphs.Contains(BoundGraph));

    // Verify the BoundGraph is garbage
    TestTrue(TEXT("BoundGraph should be marked as garbage"), !IsValid(BoundGraph));

    TestBP->MarkAsGarbage();
    return true;
}

// ── Remove nested composite cleans up recursively ───────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexGraphRemoveNestedCompositeTest,
    "Cortex.Graph.RemoveNode.NestedCompositeCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexGraphRemoveNestedCompositeTest::RunTest(const FString& Parameters)
{
    UBlueprint* TestBP = FKismetEditorUtilities::CreateBlueprint(
        AActor::StaticClass(),
        GetTransientPackage(),
        FName(*FString::Printf(TEXT("BP_CortexGraphTest_RemoveNestedComposite_%s"), *FGuid::NewGuid().ToString())),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass()
    );
    TestNotNull(TEXT("Test Blueprint should be created"), TestBP);
    if (TestBP == nullptr)
    {
        return true;
    }

    FString AssetPath = TestBP->GetPathName();
    UEdGraph* EventGraph = TestBP->UbergraphPages.Num() > 0 ? TestBP->UbergraphPages[0] : nullptr;
    TestNotNull(TEXT("EventGraph should exist"), EventGraph);

    // Create outer composite
    UK2Node_Composite* OuterComposite = NewObject<UK2Node_Composite>(EventGraph);
    OuterComposite->CreateNewGuid();
    EventGraph->AddNode(OuterComposite, true, false);
    OuterComposite->PostPlacedNewNode();
    OuterComposite->AllocateDefaultPins();

    UEdGraph* OuterBound = OuterComposite->BoundGraph;
    TestNotNull(TEXT("Outer composite should have BoundGraph"), OuterBound);

    // Create inner composite nested inside the outer one
    UK2Node_Composite* InnerComposite = NewObject<UK2Node_Composite>(OuterBound);
    InnerComposite->CreateNewGuid();
    OuterBound->AddNode(InnerComposite, true, false);
    InnerComposite->PostPlacedNewNode();
    InnerComposite->AllocateDefaultPins();

    UEdGraph* InnerBound = InnerComposite->BoundGraph;
    TestNotNull(TEXT("Inner composite should have BoundGraph"), InnerBound);

    FString OuterNodeId = OuterComposite->GetName();

    // Remove the outer composite — should recursively clean up inner too
    FCortexCommandRouter Router;
    Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.0"),
        MakeShared<FCortexGraphCommandHandler>());

    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), AssetPath);
    Params->SetStringField(TEXT("node_id"), OuterNodeId);

    FCortexCommandResult Result = Router.Execute(TEXT("graph.remove_node"), Params);
    TestTrue(TEXT("remove_node on outer composite should succeed"), Result.bSuccess);

    // Both BoundGraphs should be garbage
    TestTrue(TEXT("Outer BoundGraph should be garbage"), !IsValid(OuterBound));
    TestTrue(TEXT("Inner BoundGraph should be garbage"), !IsValid(InnerBound));

    TestBP->MarkAsGarbage();
    return true;
}
