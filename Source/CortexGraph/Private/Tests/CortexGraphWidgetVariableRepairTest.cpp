// CortexGraph Widget Variable Repair workflow integration test.
//
// Proves the UMG designer-widget repair loop end to end on the graph side:
//   is_variable=false  -> graph.add_node VariableGet fails with actionable guidance
//   (umg.set_widget_variable), and after the repair (the bit is set by the UMG
//   command, whose own mutation + transaction capture is covered by the CortexUMG
//   WidgetVariable test) the same self-context add_node succeeds.
//
// The UMG command itself lives in the CortexUMG domain module; domain modules must
// not depend on each other, so this graph-module test drives the graph side and the
// umg-module test drives the UMG side of the same workflow.

#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexGraphCommandHandler.h"
#include "Dom/JsonObject.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/BlueprintEditorUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphWidgetVariableRepairTest,
	"Cortex.Graph.WidgetVariableRepair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexGraphWidgetVariableRepairTest::RunTest(const FString& Parameters)
{
	UPackage* TestPackage = CreatePackage(TEXT("/Game/Temp/CortexGraphWidgetVariableRepairTest"));
	TestPackage->SetPackageFlags(PKG_PlayInEditor);

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(
		TestPackage, UWidgetBlueprint::StaticClass(), TEXT("WBP_RepairTest"),
		RF_Public | RF_Standalone | RF_Transactional);
	WBP->ParentClass = UUserWidget::StaticClass();
	WBP->WidgetTree = NewObject<UWidgetTree>(WBP, UWidgetTree::StaticClass(), TEXT("WidgetTree"));
	UCanvasPanel* RootCanvas = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WBP->WidgetTree->RootWidget = RootCanvas;
	UTextBlock* DesignWidget = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CommonTextBlock_147"));
	DesignWidget->bIsVariable = false;
	RootCanvas->AddChild(DesignWidget);

	// Give the WBP a mutable ubergraph so graph.add_node can resolve the EventGraph.
	UEdGraph* EventGraph = FBlueprintEditorUtils::CreateNewGraph(
		WBP, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(WBP, EventGraph);

	const FString AssetPath = WBP->GetPathName();

	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"),
		MakeShared<FCortexGraphCommandHandler>());

	auto AddVariableNode = [&Router, &AssetPath, this](const FString& NodeClass, const FString& VariableName) -> FCortexCommandResult
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("asset_path"), AssetPath);
		Params->SetStringField(TEXT("node_class"), NodeClass);
		TSharedPtr<FJsonObject> NodeParams = MakeShared<FJsonObject>();
		NodeParams->SetStringField(TEXT("variable_name"), VariableName);
		Params->SetObjectField(TEXT("params"), NodeParams);
		return Router.Execute(TEXT("graph.add_node"), Params);
	};

	// 1. Designer widget with is_variable=false must fail with the ACTIONABLE guidance
	// pointing at umg.set_widget_variable — not with a generic "Self property not found".
	FCortexCommandResult GuidedFailure = AddVariableNode(TEXT("UK2Node_VariableGet"), TEXT("CommonTextBlock_147"));
	TestFalse(TEXT("VariableGet for non-variable designer widget fails"), GuidedFailure.bSuccess);
	if (!GuidedFailure.bSuccess)
	{
		TestEqual(TEXT("guided failure is an invalid-field contract error"),
			GuidedFailure.ErrorCode, CortexErrorCodes::InvalidField);
		TestTrue(TEXT("failure message guides the umg.set_widget_variable repair"),
			GuidedFailure.ErrorMessage.Contains(TEXT("umg.set_widget_variable")));
		const TSharedPtr<FJsonObject>* Details = nullptr;
		if (GuidedFailure.ErrorDetails.IsValid()
			&& GuidedFailure.ErrorDetails->TryGetObjectField(TEXT("describe_node"), Details)
			&& Details != nullptr)
		{
			TestTrue(TEXT("error details embed the describe_node contract for retry"), (*Details).IsValid());
		}
	}

	// 2. The documented repair — umg.set_widget_variable(true) (the UMG-domain test
	// proves that command mutates the bit inside a transaction that captures the widget).
	DesignWidget->bIsVariable = true;

	// 3. After the repair the same self-context add_node must succeed.
	FCortexCommandResult AfterRepair = AddVariableNode(TEXT("UK2Node_VariableGet"), TEXT("CommonTextBlock_147"));
	TestTrue(TEXT("VariableGet succeeds after is_variable=true"), AfterRepair.bSuccess);

	// 4. VariableSet follows the same contract path.
	FCortexCommandResult SetAfterRepair = AddVariableNode(TEXT("UK2Node_VariableSet"), TEXT("CommonTextBlock_147"));
	TestTrue(TEXT("VariableSet succeeds after is_variable=true"), SetAfterRepair.bSuccess);

	WBP->MarkAsGarbage();
	TestPackage->MarkAsGarbage();
	return true;
}
