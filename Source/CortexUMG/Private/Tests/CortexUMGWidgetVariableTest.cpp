#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexAssetFingerprint.h"
#include "CortexUMGCommandHandler.h"
#include "Dom/JsonObject.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Blueprint/UserWidget.h"
#include "Editor.h"
#include "Editor/Transactor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexUMGWidgetVariableTest,
	"Cortex.UMG.WidgetVariable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGWidgetVariableTest::RunTest(const FString& Parameters)
{
	UPackage* TestPackage = CreatePackage(TEXT("/Temp/CortexUMGWidgetVariableTest"));
	// NOTE: no PKG_PlayInEditor here — FTransaction::IsTransient() treats any record
	// containing a PIE object as transient and pops the whole transaction from the undo
	// queue, which would make the transaction-capture assertions below impossible.

	UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(
		TestPackage, UWidgetBlueprint::StaticClass(), TEXT("WBP_VariableTest"),
		RF_Public | RF_Standalone | RF_Transactional);
	WBP->ParentClass = UUserWidget::StaticClass();
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
	TSharedPtr<FJsonObject> ResultFingerprint;
	if (SetResult.bSuccess && SetResult.Data.IsValid())
	{
		TestFalse(TEXT("was_variable reports false"), SetResult.Data->GetBoolField(TEXT("was_variable")));
		TestTrue(TEXT("is_variable reports true"), SetResult.Data->GetBoolField(TEXT("is_variable")));
		TestTrue(TEXT("changed is true"), SetResult.Data->GetBoolField(TEXT("changed")));
		TestTrue(TEXT("fingerprint present"), SetResult.Data->HasField(TEXT("fingerprint")));
		ResultFingerprint = SetResult.Data->GetObjectField(TEXT("fingerprint"));
	}
	TestTrue(TEXT("widget object mutated"), DesignWidget->bIsVariable);

	// The bIsVariable bit must be captured by the transaction through Widget->Modify(),
	// not lost by recording a different object. Executing editor Undo on a transient
	// Widget Blueprint can crash UE 5.6 in Kismet post-undo fixup (see
	// CortexUMGUndoRedoTest), so prove the transaction captures the widget object
	// through the transactor instead of executing Undo.
	if (GEditor == nullptr || GEditor->Trans == nullptr || !GEditor->CanTransact())
	{
		AddInfo(TEXT("Editor undo system not available - skipping transaction-capture assertions"));
	}
	else
	{
		const int32 QueueLength = GEditor->Trans->GetQueueLength();
		const FTransaction* LastTransaction = QueueLength > 0
			? GEditor->Trans->GetTransaction(QueueLength - 1)
			: nullptr;
		TestNotNull(TEXT("set_widget_variable records a transaction"), LastTransaction);
		TestTrue(TEXT("transaction captures the widget object (Widget->Modify)"),
			LastTransaction != nullptr && LastTransaction->ContainsObject(DesignWidget));
	}

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

	// Reverse transition true -> false with a matching expected_fingerprint from the mutation
	// result: proves the guard PASSES a correct fingerprint AND the reverse path mutates.
	TSharedPtr<FJsonObject> ReverseParams = MakeShared<FJsonObject>();
	ReverseParams->SetStringField(TEXT("asset_path"), AssetPath);
	ReverseParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	ReverseParams->SetBoolField(TEXT("is_variable"), false);
	ReverseParams->SetObjectField(TEXT("expected_fingerprint"), ResultFingerprint);
	FCortexCommandResult Reverse = Router.Execute(TEXT("umg.set_widget_variable"), ReverseParams);
	TestTrue(TEXT("reverse transition with matching fingerprint succeeds"), Reverse.bSuccess);
	if (Reverse.bSuccess && Reverse.Data.IsValid())
	{
		TestTrue(TEXT("reverse was_variable reports true"), Reverse.Data->GetBoolField(TEXT("was_variable")));
		TestFalse(TEXT("reverse is_variable reports false"), Reverse.Data->GetBoolField(TEXT("is_variable")));
		TestTrue(TEXT("reverse changed is true"), Reverse.Data->GetBoolField(TEXT("changed")));
	}
	TestFalse(TEXT("widget object mutated back"), DesignWidget->bIsVariable);

	// A saved-hash-only comparison misses unsaved editor changes. A fingerprint captured while the
	// package is clean must become stale when the same package transitions to dirty, even though its
	// on-disk hash has not changed.
	TestPackage->SetDirtyFlag(false);
	TSharedPtr<FJsonObject> CleanFingerprint = MakePackageNameAssetFingerprint(
		TestPackage->GetName(), TestPackage->IsDirty()).ToJson();
	TestPackage->SetDirtyFlag(true);
	TSharedPtr<FJsonObject> DirtyMismatchParams = MakeShared<FJsonObject>();
	DirtyMismatchParams->SetStringField(TEXT("asset_path"), AssetPath);
	DirtyMismatchParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	DirtyMismatchParams->SetBoolField(TEXT("is_variable"), true);
	DirtyMismatchParams->SetObjectField(TEXT("expected_fingerprint"), CleanFingerprint);
	FCortexCommandResult DirtyMismatch = Router.Execute(TEXT("umg.set_widget_variable"), DirtyMismatchParams);
	TestFalse(TEXT("clean fingerprint rejects write after package becomes dirty"), DirtyMismatch.bSuccess);
	TestEqual(TEXT("dirty mismatch error code"), DirtyMismatch.ErrorCode, CortexErrorCodes::StalePrecondition);
	TestFalse(TEXT("dirty mismatch does not mutate widget"), DesignWidget->bIsVariable);

	// Dirty-to-dirty changes must also invalidate the guard. Capture the command's current
	// fingerprint, make another unsaved widget-tree edit without changing package dirty state, then
	// prove the stale fingerprint cannot authorize the variable mutation.
	TSharedPtr<FJsonObject> DirtyFingerprintParams = MakeShared<FJsonObject>();
	DirtyFingerprintParams->SetStringField(TEXT("asset_path"), AssetPath);
	DirtyFingerprintParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	DirtyFingerprintParams->SetBoolField(TEXT("is_variable"), false);
	FCortexCommandResult DirtyFingerprintResult = Router.Execute(
		TEXT("umg.set_widget_variable"), DirtyFingerprintParams);
	TestTrue(TEXT("dirty no-op returns current fingerprint"), DirtyFingerprintResult.bSuccess);
	TSharedPtr<FJsonObject> DirtyFingerprint = DirtyFingerprintResult.bSuccess && DirtyFingerprintResult.Data.IsValid()
		? DirtyFingerprintResult.Data->GetObjectField(TEXT("fingerprint")) : nullptr;
	DesignWidget->SetIsEnabled(false);
	TSharedPtr<FJsonObject> DirtyToDirtyParams = MakeShared<FJsonObject>();
	DirtyToDirtyParams->SetStringField(TEXT("asset_path"), AssetPath);
	DirtyToDirtyParams->SetStringField(TEXT("widget_name"), TEXT("CommonTextBlock_147"));
	DirtyToDirtyParams->SetBoolField(TEXT("is_variable"), true);
	DirtyToDirtyParams->SetObjectField(TEXT("expected_fingerprint"), DirtyFingerprint);
	FCortexCommandResult DirtyToDirty = Router.Execute(TEXT("umg.set_widget_variable"), DirtyToDirtyParams);
	TestFalse(TEXT("dirty fingerprint rejects a later unsaved widget edit"), DirtyToDirty.bSuccess);
	TestEqual(TEXT("dirty-to-dirty mismatch error code"),
		DirtyToDirty.ErrorCode, CortexErrorCodes::StalePrecondition);
	TestFalse(TEXT("dirty-to-dirty mismatch does not mutate widget"), DesignWidget->bIsVariable);

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
