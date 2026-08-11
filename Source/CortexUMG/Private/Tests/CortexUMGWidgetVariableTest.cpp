#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexUMGCommandHandler.h"
#include "Dom/JsonObject.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexUMGWidgetVariableTest,
	"Cortex.UMG.WidgetVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGWidgetVariableTest::RunTest(const FString& Parameters)
{
	UPackage* TestPackage = CreatePackage(TEXT("/Game/Temp/CortexUMGWidgetVariableTest"));
	TestPackage->SetPackageFlags(PKG_PlayInEditor);

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(TestPackage, UWidgetBlueprint::StaticClass(), TEXT("WBP_VariableTest"));
	WBP->WidgetTree = NewObject<UWidgetTree>(WBP, UWidgetTree::StaticClass(), TEXT("WidgetTree"));
	UCanvasPanel* RootCanvas = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WBP->WidgetTree->RootWidget = RootCanvas;
	UTextBlock* DesignWidget = WBP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CommonTextBlock_147"));
	DesignWidget->bIsVariable = false;
	RootCanvas->AddChild(DesignWidget);

	const FString AssetPath = WBP->GetPathName();

	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("umg"), TEXT("Cortex UMG"), TEXT("1.0.1"),
		MakeShared<FCortexUMGCommandHandler>());

	TSharedPtr<FJsonObject> GetParams = MakeShared<FJsonObject>();
	GetParams->SetStringField(TEXT("asset_path"), AssetPath);
	GetParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	FCortexCommandResult GetResult = Router.Execute(TEXT("umg.get_widget"), GetParams);
	TestTrue(TEXT("get_widget succeeds"), GetResult.bSuccess);
	if (GetResult.bSuccess && GetResult.Data.IsValid())
	{
		bool bIsVariable = true;
		TestTrue(TEXT("get_widget exposes is_variable"), GetResult.Data->TryGetBoolField(TEXT("is_variable"), bIsVariable));
		TestFalse(TEXT("designer widget starts as non-variable"), bIsVariable);
	}

	TSharedPtr<FJsonObject> SetParams = MakeShared<FJsonObject>();
	SetParams->SetStringField(TEXT("asset_path"), AssetPath);
	SetParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	SetParams->SetBoolField(TEXT("is_variable"), true);
	FCortexCommandResult SetResult = Router.Execute(TEXT("umg.set_widget_variable"), SetParams);
	TestTrue(TEXT("set_widget_variable succeeds"), SetResult.bSuccess);
	if (SetResult.bSuccess && SetResult.Data.IsValid())
	{
		TestFalse(TEXT("was_variable reports false"), SetResult.Data->GetBoolField(TEXT("was_variable")));
		TestTrue(TEXT("is_variable reports true"), SetResult.Data->GetBoolField(TEXT("is_variable")));
		TestTrue(TEXT("changed is true"), SetResult.Data->GetBoolField(TEXT("changed")));
		TestTrue(TEXT("fingerprint present"), SetResult.Data->HasField(TEXT("fingerprint")));
	}
	TestTrue(TEXT("widget object mutated"), DesignWidget->bIsVariable);

	TSharedPtr<FJsonObject> RepeatParams = MakeShared<FJsonObject>();
	RepeatParams->SetStringField(TEXT("asset_path"), AssetPath);
	RepeatParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	RepeatParams->SetBoolField(TEXT("is_variable"), true);
	FCortexCommandResult Repeat = Router.Execute(TEXT("umg.set_widget_variable"), RepeatParams);
	TestTrue(TEXT("idempotent set succeeds"), Repeat.bSuccess);
	if (Repeat.bSuccess && Repeat.Data.IsValid())
	{
		TestFalse(TEXT("no-op reports changed false"), Repeat.Data->GetBoolField(TEXT("changed")));
	}

	TSharedPtr<FJsonObject> StaleParams = MakeShared<FJsonObject>();
	StaleParams->SetStringField(TEXT("asset_path"), AssetPath);
	StaleParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	StaleParams->SetBoolField(TEXT("is_variable"), true);
	TSharedPtr<FJsonObject> StaleFingerprint = MakeShared<FJsonObject>();
	StaleFingerprint->SetStringField(TEXT("package_saved_hash"), TEXT("definitely-stale-hash"));
	StaleParams->SetObjectField(TEXT("expected_fingerprint"), StaleFingerprint);
	FCortexCommandResult Stale = Router.Execute(TEXT("umg.set_widget_variable"), StaleParams);
	TestFalse(TEXT("stale fingerprint guard rejects write"), Stale.bSuccess);
	TestEqual(TEXT("stale error code"), Stale.ErrorCode, CortexErrorCodes::StalePrecondition);

	TSharedPtr<FJsonObject> MissingParams = MakeShared<FJsonObject>();
	MissingParams->SetStringField(TEXT("asset_path"), AssetPath);
	MissingParams->SetStringField(TEXT("widget_name"), TEXT("NotAWidget"));
	MissingParams->SetBoolField(TEXT("is_variable"), true);
	FCortexCommandResult Missing = Router.Execute(TEXT("umg.set_widget_variable"), MissingParams);
	TestFalse(TEXT("unknown widget fails"), Missing.bSuccess);
	TestEqual(TEXT("error code"), Missing.ErrorCode, CortexErrorCodes::WidgetNotFound);

	WBP->MarkAsGarbage();
	return true;
}
