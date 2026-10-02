#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexUMGCommandHandler.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Animation/WidgetAnimation.h"
#include "Components/CanvasPanel.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGCreateAnimationTest,
    "Cortex.UMG.CreateAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexUMGCreateAnimationTest::RunTest(const FString& Parameters)
{
    UPackage* TestPackage = CreatePackage(TEXT("/Temp/CortexUMGCreateAnimationTest"));
    UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(
        TestPackage, TEXT("WBP_AnimTest"), RF_Public | RF_Standalone | RF_Transactional);
    WBP->ParentClass = UUserWidget::StaticClass();
    WBP->WidgetTree = NewObject<UWidgetTree>(WBP, TEXT("WidgetTree"));

    const FString AssetPath = WBP->GetPathName();

    FCortexCommandRouter Router;
    Router.RegisterDomain(TEXT("umg"), TEXT("Cortex UMG"), TEXT("1.0.1"),
        MakeShared<FCortexUMGCommandHandler>());

    TSharedPtr<FJsonObject> P1 = MakeShared<FJsonObject>();
    P1->SetStringField(TEXT("asset_path"), AssetPath);
    P1->SetStringField(TEXT("widget_class"), TEXT("CanvasPanel"));
    P1->SetStringField(TEXT("name"), TEXT("Root"));
    Router.Execute(TEXT("umg.add_widget"), P1);

    TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
    CreateParams->SetStringField(TEXT("asset_path"), AssetPath);
    CreateParams->SetStringField(TEXT("animation_name"), TEXT("FadeIn"));
    CreateParams->SetNumberField(TEXT("length"), 1.0);

    FCortexCommandResult CreateResult = Router.Execute(TEXT("umg.create_animation"), CreateParams);
    TestTrue(TEXT("create_animation should succeed"), CreateResult.bSuccess);

    TSharedPtr<FJsonObject> ListParams = MakeShared<FJsonObject>();
    ListParams->SetStringField(TEXT("asset_path"), AssetPath);

    FCortexCommandResult ListResult = Router.Execute(TEXT("umg.list_animations"), ListParams);
    TestTrue(TEXT("list_animations should succeed"), ListResult.bSuccess);

    if (ListResult.Data.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Anims = nullptr;
        if (ListResult.Data->TryGetArrayField(TEXT("animations"), Anims))
        {
            TestEqual(TEXT("Should have 1 animation"), Anims->Num(), 1);
        }
    }

    WBP->MarkAsGarbage();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGRemoveAnimationVariableIdentityTest,
    "Cortex.UMG.RemoveAnimation.VariableIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexUMGRemoveAnimationVariableIdentityTest::RunTest(const FString& Parameters)
{
    UPackage* TestPackage = CreatePackage(
        *(TEXT("/Temp/CortexUMGRemoveAnimation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(
        TestPackage, TEXT("WBP_AnimationRemoval"), RF_Public | RF_Standalone | RF_Transactional);
    ON_SCOPE_EXIT
    {
        WBP->MarkAsGarbage();
        TestPackage->MarkAsGarbage();
    };
    WBP->ParentClass = UUserWidget::StaticClass();
    WBP->WidgetTree = NewObject<UWidgetTree>(WBP, TEXT("WidgetTree"));
    WBP->WidgetTree->RootWidget = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(), TEXT("Root"));

    FCortexCommandRouter Router;
    Router.RegisterDomain(TEXT("umg"), TEXT("Cortex UMG"), TEXT("1.0.1"),
        MakeShared<FCortexUMGCommandHandler>());
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    Params->SetStringField(TEXT("animation_name"), TEXT("Removed"));
    const FCortexCommandResult CreatedRemoved = Router.Execute(TEXT("umg.create_animation"), Params);
    Params->SetStringField(TEXT("animation_name"), TEXT("Retained"));
    const FCortexCommandResult CreatedRetained = Router.Execute(TEXT("umg.create_animation"), Params);
    if (!TestTrue(TEXT("Both animations are created"), CreatedRemoved.bSuccess && CreatedRetained.bSuccess))
    {
        return false;
    }

    FCompilerResultsLog InitialCompile;
    FKismetEditorUtilities::CompileBlueprint(WBP, EBlueprintCompileOptions::None, &InitialCompile);
    if (!TestEqual(TEXT("Initial animation Blueprint compiles"), InitialCompile.NumErrors, 0))
    {
        return false;
    }
    const FGuid* RemovedGuidPtr = WBP->WidgetVariableNameToGuidMap.Find(TEXT("Removed"));
    const FGuid* RetainedGuidPtr = WBP->WidgetVariableNameToGuidMap.Find(TEXT("Retained"));
    if (!TestNotNull(TEXT("Compiled deleted animation has an identity"), RemovedGuidPtr)
        || !TestNotNull(TEXT("Compiled retained animation has an identity"), RetainedGuidPtr))
    {
        return false;
    }
    const FGuid RemovedGuid = *RemovedGuidPtr;
    const FGuid RetainedGuid = *RetainedGuidPtr;
    UWidgetAnimation* const RetainedAnimation = WBP->Animations[1];

    Params->SetStringField(TEXT("animation_name"), TEXT("Removed"));
    const FCortexCommandResult Removed = Router.Execute(TEXT("umg.remove_animation"), Params);
    if (!TestTrue(TEXT("Animation removal succeeds"), Removed.bSuccess))
    {
        return false;
    }
    TestFalse(TEXT("Deleted animation identity is removed before compile"),
        WBP->WidgetVariableNameToGuidMap.Contains(TEXT("Removed")));
    TestEqual(TEXT("Retained animation identity is unchanged"),
        WBP->WidgetVariableNameToGuidMap.FindRef(TEXT("Retained")), RetainedGuid);
    TestEqual(TEXT("Only retained animation remains"), WBP->Animations.Num(), 1);
    TestTrue(TEXT("Retained animation object is unchanged"), WBP->Animations.Contains(RetainedAnimation));

    GEditor->UndoTransaction();
    TestEqual(TEXT("Undo restores both animations"), WBP->Animations.Num(), 2);
    TestEqual(TEXT("Undo restores deleted animation identity"),
        WBP->WidgetVariableNameToGuidMap.FindRef(TEXT("Removed")), RemovedGuid);
    GEditor->RedoTransaction();
    TestEqual(TEXT("Redo removes the animation again"), WBP->Animations.Num(), 1);
    TestFalse(TEXT("Redo removes deleted animation identity again"),
        WBP->WidgetVariableNameToGuidMap.Contains(TEXT("Removed")));
    TestEqual(TEXT("Redo preserves retained identity"),
        WBP->WidgetVariableNameToGuidMap.FindRef(TEXT("Retained")), RetainedGuid);
    if (WBP->WidgetVariableNameToGuidMap.Contains(TEXT("Removed")))
    {
        return false;
    }

    FCompilerResultsLog FinalCompile;
    FKismetEditorUtilities::CompileBlueprint(WBP, EBlueprintCompileOptions::None, &FinalCompile);
    TestEqual(TEXT("Blueprint compiles after animation deletion"), FinalCompile.NumErrors, 0);
    TestEqual(TEXT("Animation deletion introduces no compiler warnings"), FinalCompile.NumWarnings, 0);
    TestEqual(TEXT("Compile preserves retained animation identity"),
        WBP->WidgetVariableNameToGuidMap.FindRef(TEXT("Retained")), RetainedGuid);
    return true;
}
