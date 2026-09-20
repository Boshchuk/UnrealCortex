#include "Misc/AutomationTest.h"
#include "Tests/CortexUMGAnimationBindingTestUtils.h"
#include "Operations/CortexUMGAnimationBindingUtils.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieSceneSubSection.h"

// -----------------------------------------------------------------------------
// Step 1: Finite Proof Matrix & Stale-Key Regression Tests
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleDirtyKeyTest,
    "Cortex.UMG.AnimationBinding.StaleDirtyKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleDirtyKeyTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ChangeRetainedFloatKey(0.375f);
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves the user's edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleTangentTest,
    "Cortex.UMG.AnimationBinding.StaleTangent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleTangentTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ChangeRetainedTangent(ERichCurveTangentMode::RCTM_Break, 9.5f);
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after tangent change"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves tangent edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleDefaultValueTest,
    "Cortex.UMG.AnimationBinding.StaleDefaultValue",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleDefaultValueTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ChangeRetainedDefault(999.0f);
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after default value change"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves default value edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleBoolKeyTest,
    "Cortex.UMG.AnimationBinding.StaleBoolKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleBoolKeyTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ChangeRetainedBoolKey(false);
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after bool key change"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves bool key edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStalePlaybackRangeTest,
    "Cortex.UMG.AnimationBinding.StalePlaybackRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStalePlaybackRangeTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ChangePlaybackRange(FFrameNumber(0), FFrameNumber(12000));
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after playback range change"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves range edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleBindingGuidTest,
    "Cortex.UMG.AnimationBinding.StaleBindingGuid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleBindingGuidTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ChangeBindingGuid(0, FGuid::NewGuid());
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after binding GUID change"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves binding GUID edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleTargetRenameTest,
    "Cortex.UMG.AnimationBinding.StaleTargetRename",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleTargetRenameTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.RenameTargetWidget(0, FName(TEXT("RenamedWidget")));
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after target rename"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves target rename edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleSlotReparentTest,
    "Cortex.UMG.AnimationBinding.StaleSlotReparent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleSlotReparentTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before stale-token test"));
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Fixture.ReparentSlotWidget(0, FName(TEXT("NewSlotName")));
    const TArray<uint8> EditedState = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Old token is rejected after slot reparenting"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves slot edit"),
        EditedState == Fixture.CaptureAllAuthoredState());

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingStaleDuplicateAssetTest,
    "Cortex.UMG.AnimationBinding.StaleDuplicateAsset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingStaleDuplicateAssetTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture FixtureA(*this);
    const FCortexCommandResult ReadA = FixtureA.Router.Execute(
        TEXT("umg.list_animation_bindings"), FixtureA.InspectParams());
    if (!ReadA.bSuccess || !ReadA.Data.IsValid())
    {
        AddError(TEXT("Fixture A inspection must succeed before duplicate-asset test"));
        return false;
    }

    FCortexUMGAnimationBindingFixture FixtureB(*this);
    // Use token from Fixture A, but target Fixture B's asset_path
    TSharedPtr<FJsonObject> Params = FixtureB.RemovalParams(ReadA.Data, 0);
    // Ensure asset_path in params points to Fixture B
    Params->SetStringField(TEXT("asset_path"), FixtureB.Blueprint->GetPathName());
    const TArray<uint8> BeforeStateB = FixtureB.CaptureAllAuthoredState();

    const FCortexCommandResult Result = FixtureB.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Foreign asset token is rejected"), Result.bSuccess);
    TestEqual(TEXT("Stable stale error"), Result.ErrorCode, CortexErrorCodes::StalePrecondition);
    TestTrue(TEXT("Rejection preserves Fixture B state"),
        BeforeStateB == FixtureB.CaptureAllAuthoredState());

    return true;
}

// -----------------------------------------------------------------------------
// Step 2: Input Validation Tests
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingInputValidationTest,
    "Cortex.UMG.AnimationBinding.InputValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingInputValidationTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before validation tests"));
        return false;
    }

    const TArray<uint8> BaselineState = Fixture.CaptureAllAuthoredState();
    const bool bDirtyBefore = Fixture.Blueprint->GetPackage()->IsDirty();

    auto VerifyPreserved = [&]()
    {
        TestTrue(TEXT("Authored state preserved"), BaselineState == Fixture.CaptureAllAuthoredState());
        TestEqual(TEXT("Dirtiness preserved"), Fixture.Blueprint->GetPackage()->IsDirty(), bDirtyBefore);
    };

    // 1. Missing selector fields
    {
        // Missing binding_guid
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->RemoveField(TEXT("binding_guid"));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Missing binding_guid fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Missing widget_name
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->RemoveField(TEXT("widget_name"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Missing widget_name fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Missing slot_widget_name
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->RemoveField(TEXT("slot_widget_name"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Missing slot_widget_name fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Missing is_root_widget
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->RemoveField(TEXT("is_root_widget"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Missing is_root_widget fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Extra field in selector
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("extra_field"), TEXT("unsupported"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Extra field in selector fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();
    }

    // 2. Wrong JSON types in selector
    {
        // selector is not an object (string)
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("selector"), TEXT("not-an-object"));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Selector as string fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // binding_guid is number instead of string
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetNumberField(TEXT("binding_guid"), 12345);
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("binding_guid as number fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // is_root_widget is string instead of bool
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("is_root_widget"), TEXT("true"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("is_root_widget as string fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();
    }

    // 3. Flags and pagination validation
    {
        // dry_run="false" (string instead of bool)
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("dry_run"), TEXT("false"));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("dry_run as string fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // save=1 (number instead of bool)
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetNumberField(TEXT("save"), 1);
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("save as number fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Conflicting flags: dry_run=true, save=true
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetBoolField(TEXT("dry_run"), true);
        P->SetBoolField(TEXT("save"), true);
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("dry_run=true and save=true fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Forbidden pagination fields on removal
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetNumberField(TEXT("offset"), 0);
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("offset on removal fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetNumberField(TEXT("limit"), 10);
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("limit on removal fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("cursor"), TEXT("abc"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("cursor on removal fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();
    }

    // 4. expected_fingerprint validation
    {
        // Omitted expected_fingerprint
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->RemoveField(TEXT("expected_fingerprint"));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Omitted expected_fingerprint fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // null expected_fingerprint
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetField(TEXT("expected_fingerprint"), MakeShared<FJsonValueNull>());
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Null expected_fingerprint fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // string expected_fingerprint
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("expected_fingerprint"), TEXT("invalid_string"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("String expected_fingerprint fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();
    }

    // 5. Empty names & malformed GUID
    {
        // Empty asset_path
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("asset_path"), TEXT(""));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Empty asset_path fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Empty animation_name
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("animation_name"), TEXT(""));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Empty animation_name fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // Malformed GUID
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("binding_guid"), TEXT("not-a-valid-guid"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Malformed GUID fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();
    }

    // 6. Root widget selector constraints
    {
        // is_root_widget == true but non-empty widget_name
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetBoolField(TEXT("is_root_widget"), true);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("widget_name"), TEXT("NonEmptyWidget"));
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("slot_widget_name"), TEXT(""));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Root widget with non-empty widget_name fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // is_root_widget == true but non-empty slot_widget_name
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetBoolField(TEXT("is_root_widget"), true);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("widget_name"), TEXT(""));
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("slot_widget_name"), TEXT("NonEmptySlot"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Root widget with non-empty slot_widget_name fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();

        // is_root_widget == false but empty widget_name
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetBoolField(TEXT("is_root_widget"), false);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("widget_name"), TEXT(""));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Ordinary widget with empty widget_name fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is INVALID_FIELD"), R.ErrorCode, CortexErrorCodes::InvalidField);
        VerifyPreserved();
    }

    // 7. Missing asset, animation, or binding record
    {
        // Missing asset
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("asset_path"), TEXT("/Temp/NonExistentAssetPath_12345"));
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Missing asset fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is BLUEPRINT_NOT_FOUND"), R.ErrorCode, CortexErrorCodes::BlueprintNotFound);
        VerifyPreserved();

        // Missing animation
        P = Fixture.RemovalParams(Read.Data, 0);
        P->SetStringField(TEXT("animation_name"), TEXT("NonExistentAnim_12345"));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Missing animation fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is ANIMATION_NOT_FOUND"), R.ErrorCode, CortexErrorCodes::AnimationNotFound);
        VerifyPreserved();

        // Non-existent binding (valid GUID but no matching record)
        P = Fixture.RemovalParams(Read.Data, 0);
        P->GetObjectField(TEXT("selector"))->SetStringField(TEXT("binding_guid"),
            FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensInBraces));
        R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("Non-existent binding record fails"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is ANIMATION_BINDING_NOT_FOUND"), R.ErrorCode, CortexErrorCodes::AnimationBindingNotFound);
        VerifyPreserved();
    }

    // 7b. Ambiguous match (duplicate records)
    {
        UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
        UWidgetAnimation* Anim = nullptr;
        for (UWidgetAnimation* A : WBP->Animations)
        {
            if (A && A->GetName() == TEXT("appearance"))
            {
                Anim = A;
                break;
            }
        }
        if (Anim && Anim->AnimationBindings.Num() > 0)
        {
            // Add exact duplicate of binding 0 (copy first to avoid TArray self-reference assert)
            FWidgetAnimationBinding DupBinding = Anim->AnimationBindings[0];
            Anim->AnimationBindings.Add(DupBinding);

            // Re-read inspection to get updated fingerprint covering the duplicate
            FCortexCommandResult DupRead = Fixture.Router.Execute(
                TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
            TestTrue(TEXT("Inspection with duplicate binding succeeds"), DupRead.bSuccess);

            if (DupRead.bSuccess && DupRead.Data.IsValid())
            {
                TSharedPtr<FJsonObject> DupParams = Fixture.RemovalParams(DupRead.Data, 0);
                const TArray<uint8> StateBeforeDup = Fixture.CaptureAllAuthoredState();
                FCortexCommandResult DupResult = Fixture.Router.Execute(
                    TEXT("umg.remove_animation_binding"), DupParams);

                TestFalse(TEXT("Duplicate binding removal is rejected"), DupResult.bSuccess);
                TestEqual(TEXT("ErrorCode is ANIMATION_BINDING_AMBIGUOUS"),
                    DupResult.ErrorCode, CortexErrorCodes::AnimationBindingAmbiguous);
                TestTrue(TEXT("Ambiguous rejection preserves authored state"),
                    StateBeforeDup == Fixture.CaptureAllAuthoredState());
            }

            // Remove the temporary duplicate to restore baseline
            Anim->AnimationBindings.Pop();
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Step 2: Preview Semantics Tests
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingPreviewSemanticsTest,
    "Cortex.UMG.AnimationBinding.PreviewSemantics",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingPreviewSemanticsTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before preview test"));
        return false;
    }

    const TArray<uint8> BaselineState = Fixture.CaptureAllAuthoredState();
    const bool bDirtyBefore = Fixture.Blueprint->GetPackage()->IsDirty();

    // 1. Ordinary widget binding preview (BodySizeBox, single GUID)
    {
        TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
        Params->SetBoolField(TEXT("dry_run"), true);

        const FCortexCommandResult Result = Fixture.Router.Execute(
            TEXT("umg.remove_animation_binding"), Params);

        TestTrue(TEXT("Preview succeeds"), Result.bSuccess);
        if (Result.bSuccess && Result.Data.IsValid())
        {
            TestTrue(TEXT("dry_run is true"), Result.Data->GetBoolField(TEXT("dry_run")));
            TestFalse(TEXT("changed is false in preview"), Result.Data->GetBoolField(TEXT("changed")));
            TestFalse(TEXT("save_attempted is false"), Result.Data->GetBoolField(TEXT("save_attempted")));
            TestFalse(TEXT("saved is false"), Result.Data->GetBoolField(TEXT("saved")));

            // matched_selector
            const TSharedPtr<FJsonObject>* MatchedSel = nullptr;
            TestTrue(TEXT("matched_selector present"), Result.Data->TryGetObjectField(TEXT("matched_selector"), MatchedSel));
            if (MatchedSel && MatchedSel->IsValid())
            {
                TestEqual(TEXT("matched_selector widget_name"),
                    (*MatchedSel)->GetStringField(TEXT("widget_name")), TEXT("BodySizeBox"));
                TestEqual(TEXT("matched_selector slot_widget_name"),
                    (*MatchedSel)->GetStringField(TEXT("slot_widget_name")), TEXT(""));
                TestFalse(TEXT("matched_selector is_root_widget"),
                    (*MatchedSel)->GetBoolField(TEXT("is_root_widget")));
            }

            // before counts
            const TSharedPtr<FJsonObject>* BeforeObj = nullptr;
            TestTrue(TEXT("before counts present"), Result.Data->TryGetObjectField(TEXT("before"), BeforeObj));
            if (BeforeObj && BeforeObj->IsValid())
            {
                TestEqual(TEXT("before umg_binding_count"), (*BeforeObj)->GetIntegerField(TEXT("umg_binding_count")), 3);
                TestEqual(TEXT("before movie_scene_binding_count"), (*BeforeObj)->GetIntegerField(TEXT("movie_scene_binding_count")), 3);
                TestEqual(TEXT("before track_count"), (*BeforeObj)->GetIntegerField(TEXT("track_count")), 5);
            }

            // after counts
            const TSharedPtr<FJsonObject>* AfterObj = nullptr;
            TestTrue(TEXT("after counts present"), Result.Data->TryGetObjectField(TEXT("after"), AfterObj));
            if (AfterObj && AfterObj->IsValid())
            {
                TestEqual(TEXT("after umg_binding_count"), (*AfterObj)->GetIntegerField(TEXT("umg_binding_count")), 2);
                TestEqual(TEXT("after movie_scene_binding_count"), (*AfterObj)->GetIntegerField(TEXT("movie_scene_binding_count")), 2);
                TestEqual(TEXT("after track_count"), (*AfterObj)->GetIntegerField(TEXT("track_count")), 3);
            }

            TestTrue(TEXT("scene_data_removed is true for single GUID"),
                Result.Data->GetBoolField(TEXT("scene_data_removed")));

            // remaining_bindings
            const TArray<TSharedPtr<FJsonValue>>* RemBindings = nullptr;
            TestTrue(TEXT("remaining_bindings present"), Result.Data->TryGetArrayField(TEXT("remaining_bindings"), RemBindings));
            if (RemBindings)
            {
                TestEqual(TEXT("remaining_bindings count is 2"), RemBindings->Num(), 2);
            }

            TestFalse(TEXT("_remaining_bindings_truncated is false"),
                Result.Data->GetBoolField(TEXT("_remaining_bindings_truncated")));
            TestEqual(TEXT("_remaining_bindings_total is 2"),
                Result.Data->GetIntegerField(TEXT("_remaining_bindings_total")), 2);
        }

        // Verify zero side effects: authored state unchanged, dirtiness unchanged
        TestTrue(TEXT("Preview preserves authored state"), BaselineState == Fixture.CaptureAllAuthoredState());
        TestEqual(TEXT("Preview preserves dirtiness"), Fixture.Blueprint->GetPackage()->IsDirty(), bDirtyBefore);

        // Explicit fingerprint equality assertion before and after preview
        const FCortexCommandResult ReadAfterPreview = Fixture.Router.Execute(
            TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
        TestTrue(TEXT("Read after preview succeeds"), ReadAfterPreview.bSuccess);
        if (ReadAfterPreview.bSuccess && ReadAfterPreview.Data.IsValid())
        {
            FString DigestBefore = Read.Data->GetObjectField(TEXT("fingerprint"))->GetObjectField(TEXT("domain_signature"))->GetStringField(TEXT("digest"));
            FString DigestAfter = ReadAfterPreview.Data->GetObjectField(TEXT("fingerprint"))->GetObjectField(TEXT("domain_signature"))->GetStringField(TEXT("digest"));
            TestEqual(TEXT("Fingerprint digest unchanged before and after preview"), DigestAfter, DigestBefore);
        }
    }

    // 2. Preview with omitted dry_run (defaults to true)
    {
        TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 1);
        Params->RemoveField(TEXT("dry_run")); // Omitted dry_run

        const FCortexCommandResult Result = Fixture.Router.Execute(
            TEXT("umg.remove_animation_binding"), Params);

        TestTrue(TEXT("Preview with omitted dry_run succeeds"), Result.bSuccess);
        if (Result.bSuccess && Result.Data.IsValid())
        {
            TestTrue(TEXT("Default dry_run is true"), Result.Data->GetBoolField(TEXT("dry_run")));
            TestFalse(TEXT("changed is false"), Result.Data->GetBoolField(TEXT("changed")));
        }
        TestTrue(TEXT("Authored state preserved"), BaselineState == Fixture.CaptureAllAuthoredState());
        TestEqual(TEXT("Dirtiness preserved"), Fixture.Blueprint->GetPackage()->IsDirty(), bDirtyBefore);
    }

    // 3. Shared GUID preview: scene_data_removed must be false
    {
        // Add a second binding sharing Guid2 (BorderBody)
        UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
        UWidgetAnimation* Anim = nullptr;
        for (UWidgetAnimation* A : WBP->Animations)
        {
            if (A && A->GetName() == TEXT("appearance"))
            {
                Anim = A;
                break;
            }
        }
        TestNotNull(TEXT("Found appearance animation"), Anim);
        if (Anim && Anim->AnimationBindings.Num() >= 2)
        {
            FGuid SharedGuid = Anim->AnimationBindings[1].AnimationGuid;
            FWidgetAnimationBinding ExtraBinding;
            ExtraBinding.WidgetName = TEXT("BorderBody");
            ExtraBinding.SlotWidgetName = TEXT("ExtraSlot");
            ExtraBinding.AnimationGuid = SharedGuid;
            ExtraBinding.bIsRootWidget = false;
            Anim->AnimationBindings.Add(ExtraBinding);

            // Re-inspect with updated state
            FCortexCommandResult NewRead = Fixture.Router.Execute(
                TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
            TestTrue(TEXT("Re-inspect with shared GUID succeeds"), NewRead.bSuccess);

            if (NewRead.bSuccess && NewRead.Data.IsValid())
            {
                TSharedPtr<FJsonObject> SharedParams = Fixture.RemovalParams(NewRead.Data, 1);
                FCortexCommandResult SharedResult = Fixture.Router.Execute(
                    TEXT("umg.remove_animation_binding"), SharedParams);

                TestTrue(TEXT("Shared GUID preview succeeds"), SharedResult.bSuccess);
                if (SharedResult.bSuccess && SharedResult.Data.IsValid())
                {
                    TestFalse(TEXT("scene_data_removed is false for shared GUID"),
                        SharedResult.Data->GetBoolField(TEXT("scene_data_removed")));

                    const TSharedPtr<FJsonObject>* AfterObj = nullptr;
                    if (SharedResult.Data->TryGetObjectField(TEXT("after"), AfterObj) && AfterObj && AfterObj->IsValid())
                    {
                        const TSharedPtr<FJsonObject>* BeforeObj = nullptr;
                        SharedResult.Data->TryGetObjectField(TEXT("before"), BeforeObj);
                        if (BeforeObj && BeforeObj->IsValid())
                        {
                            TestEqual(TEXT("movie_scene_binding_count unchanged"),
                                (*AfterObj)->GetIntegerField(TEXT("movie_scene_binding_count")),
                                (*BeforeObj)->GetIntegerField(TEXT("movie_scene_binding_count")));
                            TestEqual(TEXT("track_count unchanged"),
                                (*AfterObj)->GetIntegerField(TEXT("track_count")),
                                (*BeforeObj)->GetIntegerField(TEXT("track_count")));
                        }
                    }
                }
            }
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Root Widget Binding Preview Test (Task 1 Review item)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingRootWidgetTest,
    "Cortex.UMG.AnimationBinding.RootWidget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingRootWidgetTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();

    // Create an animation with a root widget binding
    UWidgetAnimation* RootAnim = NewObject<UWidgetAnimation>(WBP, TEXT("RootAnim"), RF_Transactional);
    UMovieScene* RootMS = NewObject<UMovieScene>(RootAnim, TEXT("RootAnim"), RF_Transactional);
    RootAnim->MovieScene = RootMS;

    FGuid RootGuid = RootMS->AddPossessable(TEXT("RootUserWidget"), UUserWidget::StaticClass());

    FWidgetAnimationBinding RootBinding;
    RootBinding.WidgetName = NAME_None;
    RootBinding.SlotWidgetName = NAME_None;
    RootBinding.AnimationGuid = RootGuid;
    RootBinding.bIsRootWidget = true;
    RootAnim->AnimationBindings.Add(RootBinding);

    WBP->Animations.Add(RootAnim);

    // 1. Inspect root widget binding
    TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
    InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    InspectParams->SetStringField(TEXT("animation_name"), TEXT("RootAnim"));
    FCortexCommandResult InspectResult = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), InspectParams);

    TestTrue(TEXT("Root widget animation inspection succeeds"), InspectResult.bSuccess);
    if (InspectResult.bSuccess && InspectResult.Data.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
        InspectResult.Data->TryGetArrayField(TEXT("bindings"), Bindings);
        TestNotNull(TEXT("RootAnim bindings present"), Bindings);
        if (Bindings && Bindings->Num() == 1)
        {
            TSharedPtr<FJsonObject> B = (*Bindings)[0]->AsObject();
            TestTrue(TEXT("is_root_widget is true"), B->GetBoolField(TEXT("is_root_widget")));
            TestTrue(TEXT("target_exists is true for root widget"), B->GetBoolField(TEXT("target_exists")));
            TestEqual(TEXT("widget_name is empty for root widget"), B->GetStringField(TEXT("widget_name")), TEXT(""));
            TestEqual(TEXT("slot_widget_name is empty for root widget"), B->GetStringField(TEXT("slot_widget_name")), TEXT(""));
        }

        // 2. Preview removal of root widget binding
        TSharedPtr<FJsonObject> RemoveParams = MakeShared<FJsonObject>();
        RemoveParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
        RemoveParams->SetStringField(TEXT("animation_name"), TEXT("RootAnim"));

        TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
        Selector->SetStringField(TEXT("binding_guid"), RootGuid.ToString(EGuidFormats::DigitsWithHyphensInBraces));
        Selector->SetStringField(TEXT("widget_name"), TEXT(""));
        Selector->SetStringField(TEXT("slot_widget_name"), TEXT(""));
        Selector->SetBoolField(TEXT("is_root_widget"), true);
        RemoveParams->SetObjectField(TEXT("selector"), Selector);

        RemoveParams->SetObjectField(TEXT("expected_fingerprint"),
            InspectResult.Data->GetObjectField(TEXT("fingerprint")));
        RemoveParams->SetBoolField(TEXT("dry_run"), true);

        FCortexCommandResult RemoveResult = Fixture.Router.Execute(
            TEXT("umg.remove_animation_binding"), RemoveParams);

        TestTrue(TEXT("Root widget preview removal succeeds"), RemoveResult.bSuccess);
        if (RemoveResult.bSuccess && RemoveResult.Data.IsValid())
        {
            TestTrue(TEXT("Root preview dry_run is true"), RemoveResult.Data->GetBoolField(TEXT("dry_run")));
            TestEqual(TEXT("after.umg_binding_count is 0"),
                RemoveResult.Data->GetObjectField(TEXT("after"))->GetIntegerField(TEXT("umg_binding_count")), 0);
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Step 3: Mutation Apply & Relationship / Lifetime Tests (Task 3)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingRemovePreservesRetainedTest,
    "Cortex.UMG.AnimationBinding.RemovePreservesRetained",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingRemovePreservesRetainedTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    WBP->GetPackage()->ClearDirtyFlag();
    const bool bDirtyBefore = WBP->GetPackage()->IsDirty();
    TestFalse(TEXT("Package starts clean"), bDirtyBefore);

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before removal test"));
        return false;
    }

    UWidgetAnimation* Anim = nullptr;
    for (UWidgetAnimation* A : WBP->Animations)
    {
        if (A && A->GetName() == TEXT("appearance"))
        {
            Anim = A;
            break;
        }
    }
    TestNotNull(TEXT("appearance animation exists"), Anim);
    if (!Anim || !Anim->MovieScene)
    {
        return false;
    }
    UMovieScene* MS = Anim->MovieScene;

    // Guid to remove is binding 0 (BodySizeBox, Guid1)
    const FGuid RemovedGuid = Anim->AnimationBindings[0].AnimationGuid;

    // Capture complete native retained state before apply
    const TArray<uint8> RetainedBefore = Fixture.CaptureRetainedAuthoredState(RemovedGuid);

    // Apply with dry_run=false
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);

    const FCortexCommandResult Applied = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Apply succeeds"), Applied.bSuccess);
    if (!Applied.bSuccess || !Applied.Data.IsValid())
    {
        return false;
    }

    // JSON response assertions
    TestFalse(TEXT("dry_run is false"), Applied.Data->GetBoolField(TEXT("dry_run")));
    TestTrue(TEXT("A mutation occurred"), Applied.Data->GetBoolField(TEXT("changed")));
    TestFalse(TEXT("Default does not save"), Applied.Data->GetBoolField(TEXT("saved")));
    TestFalse(TEXT("save_attempted is false"), Applied.Data->GetBoolField(TEXT("save_attempted")));
    TestTrue(TEXT("scene_data_removed is true for unshared GUID"),
        Applied.Data->GetBoolField(TEXT("scene_data_removed")));

    const TSharedPtr<FJsonObject> BeforeObj = Applied.Data->GetObjectField(TEXT("before"));
    TestEqual(TEXT("Before UMG bindings is 3"), BeforeObj->GetIntegerField(TEXT("umg_binding_count")), 3);
    TestEqual(TEXT("Before MovieScene bindings is 3"), BeforeObj->GetIntegerField(TEXT("movie_scene_binding_count")), 3);
    TestEqual(TEXT("Before track count is 5"), BeforeObj->GetIntegerField(TEXT("track_count")), 5);

    const TSharedPtr<FJsonObject> AfterObj = Applied.Data->GetObjectField(TEXT("after"));
    TestEqual(TEXT("Two records remain in after.umg_binding_count"),
        AfterObj->GetIntegerField(TEXT("umg_binding_count")), 2);
    TestEqual(TEXT("Two records remain in after.movie_scene_binding_count"),
        AfterObj->GetIntegerField(TEXT("movie_scene_binding_count")), 2);
    TestEqual(TEXT("Three tracks remain in after.track_count"),
        AfterObj->GetIntegerField(TEXT("track_count")), 3);

    // Assert native state: exactly one metadata record is gone
    TestEqual(TEXT("Native UMG bindings count is 2"), Anim->AnimationBindings.Num(), 2);
    for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
    {
        TestNotEqual(TEXT("Removed GUID not in remaining UMG bindings"), B.AnimationGuid, RemovedGuid);
    }

    // Unshared scene binding and possessable are gone
    TestNull(TEXT("MovieScene possessable removed"), MS->FindPossessable(RemovedGuid));
    TestNull(TEXT("MovieScene binding removed"), MS->FindBinding(RemovedGuid));

    // Package is marked dirty in memory, not saved
    TestTrue(TEXT("Package marked dirty after mutation"), WBP->GetPackage()->IsDirty());

    // Compare retained data byte-for-byte
    const TArray<uint8> RetainedAfter = Fixture.CaptureRetainedAuthoredState(RemovedGuid);
    TestTrue(TEXT("All retained authored data preserved byte-for-byte"), RetainedBefore == RetainedAfter);

    // Deep native assertions on retained targets, tracks, sections, channels, keys, tangents, defaults
    // 1. Guid2 (BorderBody): RenderOpacity
    const FGuid Guid2 = Anim->AnimationBindings[0].AnimationGuid;
    TestNotNull(TEXT("BorderBody possessable retained"), MS->FindPossessable(Guid2));
    const FMovieSceneBinding* Binding2 = MS->FindBinding(Guid2);
    TestNotNull(TEXT("BorderBody binding retained"), Binding2);
    if (Binding2)
    {
        TestEqual(TEXT("BorderBody has 1 track"), Binding2->GetTracks().Num(), 1);
        UMovieSceneFloatTrack* OpacityTrack = Cast<UMovieSceneFloatTrack>(Binding2->GetTracks()[0]);
        TestNotNull(TEXT("RenderOpacity track retained"), OpacityTrack);
        if (OpacityTrack && OpacityTrack->GetAllSections().Num() > 0)
        {
            UMovieSceneFloatSection* OpacitySec = Cast<UMovieSceneFloatSection>(OpacityTrack->GetAllSections()[0]);
            TestNotNull(TEXT("RenderOpacity section retained"), OpacitySec);
            if (OpacitySec)
            {
                TestEqual(TEXT("RenderOpacity default is 0.0"), OpacitySec->GetChannel().GetDefault().Get(1.0f), 0.0f);
                TestEqual(TEXT("RenderOpacity has 2 keys"), OpacitySec->GetChannel().GetTimes().Num(), 2);
            }
        }
    }

    // 2. Guid3 (StorylineIcon): bIsEnabled
    const FGuid Guid3 = Anim->AnimationBindings[1].AnimationGuid;
    TestNotNull(TEXT("StorylineIcon possessable retained"), MS->FindPossessable(Guid3));
    const FMovieSceneBinding* Binding3 = MS->FindBinding(Guid3);
    TestNotNull(TEXT("StorylineIcon binding retained"), Binding3);
    if (Binding3)
    {
        TestEqual(TEXT("StorylineIcon has 1 track"), Binding3->GetTracks().Num(), 1);
        UMovieSceneBoolTrack* IsEnabledTrack = Cast<UMovieSceneBoolTrack>(Binding3->GetTracks()[0]);
        TestNotNull(TEXT("bIsEnabled track retained"), IsEnabledTrack);
        if (IsEnabledTrack && IsEnabledTrack->GetAllSections().Num() > 0)
        {
            UMovieSceneBoolSection* IsEnabledSec = Cast<UMovieSceneBoolSection>(IsEnabledTrack->GetAllSections()[0]);
            TestNotNull(TEXT("bIsEnabled section retained"), IsEnabledSec);
            if (IsEnabledSec)
            {
                TestEqual(TEXT("bIsEnabled default is true"), IsEnabledSec->GetChannel().GetDefault().Get(false), true);
                TestEqual(TEXT("bIsEnabled has 2 keys"), IsEnabledSec->GetChannel().GetTimes().Num(), 2);
            }
        }
    }

    // 3. Playback range, frame rates, master event track
    const UMovieScene* ConstMS = MS;
    TestEqual(TEXT("Playback range start is 120"), ConstMS->GetPlaybackRange().GetLowerBoundValue().Value, 120);
    TestEqual(TEXT("Playback range end is 720"), ConstMS->GetPlaybackRange().GetUpperBoundValue().Value, 720);
    TestEqual(TEXT("Master event track retained"), ConstMS->GetTracks().Num(), 1);

    // 4. Sibling animation 'idle' retained
    TestEqual(TEXT("WBP has 2 animations"), WBP->Animations.Num(), 2);
    TestEqual(TEXT("Second anim is idle"), WBP->Animations[1]->GetName(), TEXT("idle"));

    // 5. All widgets retained in WidgetTree
    TestNotNull(TEXT("BodySizeBox widget retained in tree"), WBP->WidgetTree->FindWidget(TEXT("BodySizeBox")));
    TestNotNull(TEXT("BorderBody widget retained in tree"), WBP->WidgetTree->FindWidget(TEXT("BorderBody")));
    TestNotNull(TEXT("StorylineIcon widget retained in tree"), WBP->WidgetTree->FindWidget(TEXT("StorylineIcon")));

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingSharedGuidPreservationTest,
    "Cortex.UMG.AnimationBinding.SharedGuidPreservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingSharedGuidPreservationTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    UWidgetAnimation* Anim = nullptr;
    for (UWidgetAnimation* A : WBP->Animations)
    {
        if (A && A->GetName() == TEXT("appearance"))
        {
            Anim = A;
            break;
        }
    }
    TestNotNull(TEXT("appearance animation exists"), Anim);
    if (!Anim || !Anim->MovieScene || Anim->AnimationBindings.Num() < 2)
    {
        return false;
    }
    UMovieScene* MS = Anim->MovieScene;

    // Add a second binding sharing Guid2 (BorderBody)
    const FGuid SharedGuid = Anim->AnimationBindings[1].AnimationGuid;
    FWidgetAnimationBinding ExtraBinding;
    ExtraBinding.WidgetName = TEXT("BorderBody");
    ExtraBinding.SlotWidgetName = TEXT("ExtraSlot");
    ExtraBinding.AnimationGuid = SharedGuid;
    ExtraBinding.bIsRootWidget = false;
    Anim->AnimationBindings.Add(ExtraBinding);

    // Now 4 UMG bindings, 3 MovieScene bindings, Guid2 sharing count = 2
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect with shared GUID succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    // Remove the extra binding (find index where slot_widget_name == "ExtraSlot")
    int32 ExtraIndex = -1;
    const TArray<TSharedPtr<FJsonValue>>* BindingsArray = nullptr;
    if (Read.Data->TryGetArrayField(TEXT("bindings"), BindingsArray) && BindingsArray)
    {
        for (int32 i = 0; i < BindingsArray->Num(); ++i)
        {
            if ((*BindingsArray)[i]->AsObject()->GetStringField(TEXT("slot_widget_name")) == TEXT("ExtraSlot"))
            {
                ExtraIndex = i;
                break;
            }
        }
    }
    TestTrue(TEXT("Found extra binding index"), ExtraIndex != -1);
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, ExtraIndex);
    Params->SetBoolField(TEXT("dry_run"), false);

    const FCortexCommandResult Applied = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Shared GUID removal succeeds"), Applied.bSuccess);
    if (!Applied.bSuccess || !Applied.Data.IsValid())
    {
        return false;
    }

    // Response assertions
    TestTrue(TEXT("changed is true"), Applied.Data->GetBoolField(TEXT("changed")));
    TestFalse(TEXT("scene_data_removed is false for shared GUID"),
        Applied.Data->GetBoolField(TEXT("scene_data_removed")));

    const TSharedPtr<FJsonObject> AfterObj = Applied.Data->GetObjectField(TEXT("after"));
    TestEqual(TEXT("after.umg_binding_count is 3"), AfterObj->GetIntegerField(TEXT("umg_binding_count")), 3);
    TestEqual(TEXT("after.movie_scene_binding_count is 3 (unchanged)"),
        AfterObj->GetIntegerField(TEXT("movie_scene_binding_count")), 3);
    TestEqual(TEXT("after.track_count is 5 (unchanged)"),
        AfterObj->GetIntegerField(TEXT("track_count")), 5);

    // Native assertions: exactly 1 UMG binding removed, MovieScene possessable and tracks intact
    TestEqual(TEXT("Native UMG bindings count is 3"), Anim->AnimationBindings.Num(), 3);
    TestNotNull(TEXT("MovieScene possessable preserved"), MS->FindPossessable(SharedGuid));
    TestNotNull(TEXT("MovieScene binding preserved"), MS->FindBinding(SharedGuid));
    const FMovieSceneBinding* MSB = MS->FindBinding(SharedGuid);
    if (MSB)
    {
        TestEqual(TEXT("MovieScene tracks preserved on shared possessable"), MSB->GetTracks().Num(), 1);
    }

    // Original BorderBody binding still in AnimationBindings
    bool bFoundOriginalBorderBody = false;
    for (const FWidgetAnimationBinding& B : Anim->AnimationBindings)
    {
        if (B.WidgetName == TEXT("BorderBody") && B.SlotWidgetName == NAME_None && B.AnimationGuid == SharedGuid)
        {
            bFoundOriginalBorderBody = true;
            break;
        }
    }
    TestTrue(TEXT("Original BorderBody binding remains"), bFoundOriginalBorderBody);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingLastBindingPreservesAnimationTest,
    "Cortex.UMG.AnimationBinding.LastBindingPreservesAnimation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingLastBindingPreservesAnimationTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();

    // Create a standalone animation with exactly 1 binding and 1 master event track
    UWidgetAnimation* SingleAnim = NewObject<UWidgetAnimation>(WBP, TEXT("single_bind_anim"), RF_Transactional);
    UMovieScene* SingleMS = NewObject<UMovieScene>(SingleAnim, TEXT("single_bind_anim"), RF_Transactional);
    SingleAnim->MovieScene = SingleMS;
    SingleMS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(24000)));

    USizeBox* BodySizeBox = Cast<USizeBox>(WBP->WidgetTree->FindWidget(TEXT("BodySizeBox")));
    TestNotNull(TEXT("BodySizeBox exists"), BodySizeBox);
    FGuid SingleGuid = SingleMS->AddPossessable(TEXT("BodySizeBox"), BodySizeBox->GetClass());

    FWidgetAnimationBinding SingleB;
    SingleB.WidgetName = TEXT("BodySizeBox");
    SingleB.SlotWidgetName = NAME_None;
    SingleB.AnimationGuid = SingleGuid;
    SingleB.bIsRootWidget = false;
    SingleAnim->AnimationBindings.Add(SingleB);

    UMovieSceneFloatTrack* Track = SingleMS->AddTrack<UMovieSceneFloatTrack>(SingleGuid);
    Track->SetPropertyNameAndPath(FName("WidthOverride"), TEXT("WidthOverride"));
    UMovieSceneFloatSection* Sec = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
    Track->AddSection(*Sec);
    Sec->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(24000)));

    UMovieSceneEventTrack* EventTrack = SingleMS->AddTrack<UMovieSceneEventTrack>();
    UMovieSceneSection* EventSec = EventTrack->CreateNewSection();
    if (EventSec)
    {
        EventTrack->AddSection(*EventSec);
        EventSec->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(24000)));
    }

    WBP->Animations.Add(SingleAnim);

    // Inspect
    TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
    InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    InspectParams->SetStringField(TEXT("animation_name"), TEXT("single_bind_anim"));
    FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
    TestTrue(TEXT("Inspect single_bind_anim succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    // Remove the only binding
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetStringField(TEXT("animation_name"), TEXT("single_bind_anim"));
    Params->SetBoolField(TEXT("dry_run"), false);

    const FCortexCommandResult Applied = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Remove last binding succeeds"), Applied.bSuccess);
    if (!Applied.bSuccess || !Applied.Data.IsValid())
    {
        return false;
    }

    TestTrue(TEXT("scene_data_removed is true"), Applied.Data->GetBoolField(TEXT("scene_data_removed")));
    const TSharedPtr<FJsonObject> AfterObj = Applied.Data->GetObjectField(TEXT("after"));
    TestEqual(TEXT("after.umg_binding_count is 0"), AfterObj->GetIntegerField(TEXT("umg_binding_count")), 0);
    TestEqual(TEXT("after.movie_scene_binding_count is 0"), AfterObj->GetIntegerField(TEXT("movie_scene_binding_count")), 0);
    TestEqual(TEXT("after.track_count is 1 (event track)"), AfterObj->GetIntegerField(TEXT("track_count")), 1);

    // Native assertions: animation object, MovieScene, playback range, and event track remain
    TestTrue(TEXT("SingleAnim still in WBP->Animations"), WBP->Animations.Contains(SingleAnim));
    TestNotNull(TEXT("MovieScene still exists"), SingleAnim->MovieScene.Get());
    const UMovieScene* ConstSingleMS = SingleMS;
    TestEqual(TEXT("SingleMS has 0 possessables"), ConstSingleMS->GetPossessableCount(), 0);
    TestEqual(TEXT("SingleMS has 0 bindings"), ConstSingleMS->GetBindings().Num(), 0);
    TestEqual(TEXT("SingleMS event track preserved"), ConstSingleMS->GetTracks().Num(), 1);
    TestEqual(TEXT("Playback range preserved"), ConstSingleMS->GetPlaybackRange().GetUpperBoundValue().Value, 24000);
    TestEqual(TEXT("AnimationBindings is empty"), SingleAnim->AnimationBindings.Num(), 0);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingSlotAndRootRemovalTest,
    "Cortex.UMG.AnimationBinding.SlotAndRootRemoval",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingSlotAndRootRemovalTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();

    // 1. Test Slot Widget removal
    {
        UWidgetAnimation* SlotAnim = NewObject<UWidgetAnimation>(WBP, TEXT("slot_anim"), RF_Transactional);
        UMovieScene* SlotMS = NewObject<UMovieScene>(SlotAnim, TEXT("slot_anim"), RF_Transactional);
        SlotAnim->MovieScene = SlotMS;
        SlotMS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));

        FGuid SlotGuid = SlotMS->AddPossessable(TEXT("BodySizeBox.Slot"), UPanelSlot::StaticClass());
        FWidgetAnimationBinding SlotB;
        SlotB.WidgetName = TEXT("BodySizeBox");
        SlotB.SlotWidgetName = TEXT("CanvasSlot");
        SlotB.AnimationGuid = SlotGuid;
        SlotB.bIsRootWidget = false;
        SlotAnim->AnimationBindings.Add(SlotB);

        WBP->Animations.Add(SlotAnim);

        TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
        InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
        InspectParams->SetStringField(TEXT("animation_name"), TEXT("slot_anim"));
        FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
        TestTrue(TEXT("Inspect slot_anim succeeds"), Read.bSuccess);

        if (Read.bSuccess && Read.Data.IsValid())
        {
            TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
            Params->SetStringField(TEXT("animation_name"), TEXT("slot_anim"));
            Params->SetBoolField(TEXT("dry_run"), false);

            FCortexCommandResult Applied = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), Params);
            TestTrue(TEXT("Slot binding removal succeeds"), Applied.bSuccess);
            if (Applied.bSuccess && Applied.Data.IsValid())
            {
                TestTrue(TEXT("changed is true"), Applied.Data->GetBoolField(TEXT("changed")));
                TestEqual(TEXT("after.umg_binding_count is 0"),
                    Applied.Data->GetObjectField(TEXT("after"))->GetIntegerField(TEXT("umg_binding_count")), 0);
                TestNull(TEXT("Slot possessable removed from MovieScene"), SlotMS->FindPossessable(SlotGuid));
            }
        }
    }

    // 2. Test Root Widget removal
    {
        UWidgetAnimation* RootAnim = NewObject<UWidgetAnimation>(WBP, TEXT("root_anim_apply"), RF_Transactional);
        UMovieScene* RootMS = NewObject<UMovieScene>(RootAnim, TEXT("root_anim_apply"), RF_Transactional);
        RootAnim->MovieScene = RootMS;
        RootMS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));

        FGuid RootGuid = RootMS->AddPossessable(TEXT("RootUserWidget"), UUserWidget::StaticClass());
        FWidgetAnimationBinding RootB;
        RootB.WidgetName = NAME_None;
        RootB.SlotWidgetName = NAME_None;
        RootB.AnimationGuid = RootGuid;
        RootB.bIsRootWidget = true;
        RootAnim->AnimationBindings.Add(RootB);

        WBP->Animations.Add(RootAnim);

        TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
        InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
        InspectParams->SetStringField(TEXT("animation_name"), TEXT("root_anim_apply"));
        FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
        TestTrue(TEXT("Inspect root_anim_apply succeeds"), Read.bSuccess);

        if (Read.bSuccess && Read.Data.IsValid())
        {
            TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
            Params->SetStringField(TEXT("animation_name"), TEXT("root_anim_apply"));
            Params->SetBoolField(TEXT("dry_run"), false);

            FCortexCommandResult Applied = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), Params);
            TestTrue(TEXT("Root binding removal succeeds"), Applied.bSuccess);
            if (Applied.bSuccess && Applied.Data.IsValid())
            {
                TestTrue(TEXT("changed is true"), Applied.Data->GetBoolField(TEXT("changed")));
                TestEqual(TEXT("after.umg_binding_count is 0"),
                    Applied.Data->GetObjectField(TEXT("after"))->GetIntegerField(TEXT("umg_binding_count")), 0);
                TestNull(TEXT("Root possessable removed from MovieScene"), RootMS->FindPossessable(RootGuid));
            }
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingUndoRedoTest,
    "Cortex.UMG.AnimationBinding.UndoRedo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingUndoRedoTest::RunTest(const FString& Parameters)
{
    if (GEditor == nullptr || GEditor->Trans == nullptr || !GEditor->CanTransact())
    {
        AddInfo(TEXT("Editor undo system not available - skipping"));
        return true;
    }

    GEditor->ResetTransaction(FText::FromString(TEXT("Cortex UMG Animation Binding Undo Test Setup")));

    FCortexUMGAnimationBindingFixture Fixture(*this);
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        AddError(TEXT("Fixture inspection must succeed before undo test"));
        return false;
    }

    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    UWidgetAnimation* Anim = nullptr;
    for (UWidgetAnimation* A : WBP->Animations)
    {
        if (A && A->GetName() == TEXT("appearance"))
        {
            Anim = A;
            break;
        }
    }
    TestNotNull(TEXT("appearance animation exists"), Anim);
    if (!Anim || !Anim->MovieScene)
    {
        return false;
    }
    UMovieScene* MS = Anim->MovieScene;

    const FGuid GuidToRemove = Anim->AnimationBindings[0].AnimationGuid;

    // Capture complete authored state before removal
    const TArray<uint8> StateBefore = Fixture.CaptureAllAuthoredState();

    // Apply removal of binding 0 (BodySizeBox)
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);

    const FCortexCommandResult Applied = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Apply succeeds"), Applied.bSuccess);
    if (!Applied.bSuccess)
    {
        return false;
    }

    const TArray<uint8> StateAfter = Fixture.CaptureAllAuthoredState();
    TestTrue(TEXT("Authored state changed after removal"), StateBefore != StateAfter);
    TestEqual(TEXT("UMG bindings count is 2"), Anim->AnimationBindings.Num(), 2);
    TestNull(TEXT("Possessable removed"), MS->FindPossessable(GuidToRemove));

    // Perform Undo
    const bool bUndoSuccess = GEditor->UndoTransaction();
    TestTrue(TEXT("UndoTransaction succeeds"), bUndoSuccess);

    // GC pass to verify transaction roots the restored objects
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

    // Verify restored state
    const TArray<uint8> StateAfterUndo = Fixture.CaptureAllAuthoredState();
    TestTrue(TEXT("Full authored state restored after Undo + GC"), StateAfterUndo == StateBefore);
    TestEqual(TEXT("UMG bindings count restored to 3"), Anim->AnimationBindings.Num(), 3);
    TestNotNull(TEXT("Possessable restored"), MS->FindPossessable(GuidToRemove));
    TestNotNull(TEXT("Binding restored"), MS->FindBinding(GuidToRemove));

    // Perform Redo
    const bool bRedoSuccess = GEditor->RedoTransaction();
    TestTrue(TEXT("RedoTransaction succeeds"), bRedoSuccess);

    // Verify re-applied state
    const TArray<uint8> StateAfterRedo = Fixture.CaptureAllAuthoredState();
    TestTrue(TEXT("Full authored state matches post-removal state after Redo"), StateAfterRedo == StateAfter);
    TestEqual(TEXT("UMG bindings count is 2 after Redo"), Anim->AnimationBindings.Num(), 2);
    TestNull(TEXT("Possessable removed after Redo"), MS->FindPossessable(GuidToRemove));

    GEditor->ResetTransaction(FText::FromString(TEXT("Cortex UMG Animation Binding Undo Test Cleanup")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingFailureRestorationTest,
    "Cortex.UMG.AnimationBinding.FailureRestoration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingFailureRestorationTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    UWidgetAnimation* Anim = nullptr;
    for (UWidgetAnimation* A : WBP->Animations)
    {
        if (A && A->GetName() == TEXT("appearance"))
        {
            Anim = A;
            break;
        }
    }
    TestNotNull(TEXT("appearance animation exists"), Anim);
    if (!Anim || !Anim->MovieScene)
    {
        return false;
    }
    UMovieScene* MS = Anim->MovieScene;

    // Ensure package is initially clean
    WBP->GetPackage()->ClearDirtyFlag();
    const bool bInitiallyDirty = WBP->GetPackage()->IsDirty();
    TestFalse(TEXT("Package starts clean"), bInitiallyDirty);

    const TArray<uint8> StateBefore = Fixture.CaptureAllAuthoredState();

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    // 1. Injected failure after UMG removal
    CortexUMGAnimationBindingUtils::SetFailureInjection(
        CortexUMGAnimationBindingUtils::EFailureInjection::FailAfterUMGRemoval);

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);

    const FCortexCommandResult FailedResult = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    // Verify failure and that error is NOT DirtyEditorState (since restoration succeeded)
    TestFalse(TEXT("Operation failed as injected"), FailedResult.bSuccess);
    TestNotEqual(TEXT("Error is not DIRTY_EDITOR_STATE because restoration succeeded"),
        FailedResult.ErrorCode, CortexErrorCodes::DirtyEditorState);

    // Verify both representations are restored
    TestEqual(TEXT("AnimationBindings restored to 3"), Anim->AnimationBindings.Num(), 3);
    const FGuid Guid0 = Anim->AnimationBindings[0].AnimationGuid;
    TestNotNull(TEXT("MovieScene possessable intact"), MS->FindPossessable(Guid0));
    TestNotNull(TEXT("MovieScene binding intact"), MS->FindBinding(Guid0));

    // Verify complete authored state is restored
    const TArray<uint8> StateAfterFail = Fixture.CaptureAllAuthoredState();
    TestTrue(TEXT("Authored state restored completely"), StateAfterFail == StateBefore);

    // Verify pre-existing dirty state is restored (clean)
    TestFalse(TEXT("Package dirty state restored to clean"), WBP->GetPackage()->IsDirty());

    // 2. Injected failure with failed restoration verification -> must return DIRTY_EDITOR_STATE
    CortexUMGAnimationBindingUtils::SetFailureInjection(
        CortexUMGAnimationBindingUtils::EFailureInjection::FailRestorationVerification);

    const FCortexCommandResult UnverifiedResult = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Unverified restoration operation fails"), UnverifiedResult.bSuccess);
    TestEqual(TEXT("Unverified restoration returns DIRTY_EDITOR_STATE"),
        UnverifiedResult.ErrorCode, CortexErrorCodes::DirtyEditorState);

    // Reset failure hook
    CortexUMGAnimationBindingUtils::SetFailureInjection(
        CortexUMGAnimationBindingUtils::EFailureInjection::None);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingOrphanedTrackRejectionTest,
    "Cortex.UMG.AnimationBinding.OrphanedTrackRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingOrphanedTrackRejectionTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();

    UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, TEXT("orphaned_track_anim"), RF_Transactional);
    UMovieScene* MS = NewObject<UMovieScene>(Anim, TEXT("orphaned_track_anim"), RF_Transactional);
    Anim->MovieScene = MS;
    MS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));

    // Add possessable so we get a binding and can add a track to it
    FGuid OrphanGuid = MS->AddPossessable(TEXT("OrphanTarget"), UUserWidget::StaticClass());
    UMovieSceneFloatTrack* FloatTrack = MS->AddTrack<UMovieSceneFloatTrack>(OrphanGuid);
    FloatTrack->SetPropertyNameAndPath(FName("RenderOpacity"), TEXT("RenderOpacity"));
    UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(FloatTrack->CreateNewSection());
    FloatTrack->AddSection(*Section);
    Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));

    // Now remove the possessable from MovieScene's Possessables array using reflection,
    // leaving the FMovieSceneBinding with its tracks orphaned without a possessable.
    FArrayProperty* PossessablesProp = FindFProperty<FArrayProperty>(UMovieScene::StaticClass(), TEXT("Possessables"));
    if (!PossessablesProp)
    {
        AddError(TEXT("Failed to find Possessables property on UMovieScene"));
        return false;
    }
    FScriptArrayHelper Helper(PossessablesProp, PossessablesProp->ContainerPtrToValuePtr<void>(MS));
    for (int32 i = 0; i < Helper.Num(); ++i)
    {
        const FMovieScenePossessable* P = reinterpret_cast<const FMovieScenePossessable*>(Helper.GetRawPtr(i));
        if (P && P->GetGuid() == OrphanGuid)
        {
            Helper.RemoveValues(i, 1);
            break;
        }
    }

    // Verify setup: possessable is absent, but binding exists and has tracks
    TestNull(TEXT("Possessable is absent"), MS->FindPossessable(OrphanGuid));
    const FMovieSceneBinding* Binding = MS->FindBinding(OrphanGuid);
    TestNotNull(TEXT("Binding exists"), Binding);
    TestTrue(TEXT("Binding has tracks"), Binding && Binding->GetTracks().Num() > 0);

    // Add UMG binding referencing OrphanGuid
    FWidgetAnimationBinding OrphanB;
    OrphanB.WidgetName = FName(TEXT("OrphanTarget"));
    OrphanB.SlotWidgetName = NAME_None;
    OrphanB.AnimationGuid = OrphanGuid;
    OrphanB.bIsRootWidget = false;
    Anim->AnimationBindings.Add(OrphanB);

    WBP->Animations.Add(Anim);

    // Inspect
    TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
    InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    InspectParams->SetStringField(TEXT("animation_name"), TEXT("orphaned_track_anim"));
    FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    // Attempt removal -> must be rejected with ANIMATION_BINDING_UNSUPPORTED
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetStringField(TEXT("animation_name"), TEXT("orphaned_track_anim"));
    Params->SetBoolField(TEXT("dry_run"), false);

    FCortexCommandResult Result = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), Params);
    TestFalse(TEXT("Orphaned track binding removal is rejected"), Result.bSuccess);
    TestEqual(TEXT("Error code is ANIMATION_BINDING_UNSUPPORTED"),
        Result.ErrorCode, CortexErrorCodes::AnimationBindingUnsupported);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingDanglingTargetRemovalTest,
    "Cortex.UMG.AnimationBinding.DanglingTargetRemoval",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingDanglingTargetRemovalTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();

    UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, TEXT("dangling_target_anim"), RF_Transactional);
    UMovieScene* MS = NewObject<UMovieScene>(Anim, TEXT("dangling_target_anim"), RF_Transactional);
    Anim->MovieScene = MS;
    MS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));

    // Create possessable in MovieScene for a widget name that does NOT exist in WidgetTree
    const FName MissingWidgetName(TEXT("DeletedWidget_NonExistent"));
    FGuid DanglingGuid = MS->AddPossessable(MissingWidgetName.ToString(), UUserWidget::StaticClass());

    UMovieSceneFloatTrack* FloatTrack = MS->AddTrack<UMovieSceneFloatTrack>(DanglingGuid);
    FloatTrack->SetPropertyNameAndPath(FName("RenderOpacity"), TEXT("RenderOpacity"));
    UMovieSceneFloatSection* Section = Cast<UMovieSceneFloatSection>(FloatTrack->CreateNewSection());
    FloatTrack->AddSection(*Section);
    Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));

    FWidgetAnimationBinding DanglingB;
    DanglingB.WidgetName = MissingWidgetName;
    DanglingB.SlotWidgetName = NAME_None;
    DanglingB.AnimationGuid = DanglingGuid;
    DanglingB.bIsRootWidget = false;
    Anim->AnimationBindings.Add(DanglingB);

    WBP->Animations.Add(Anim);

    // Inspect and verify target_exists is false
    TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
    InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    InspectParams->SetStringField(TEXT("animation_name"), TEXT("dangling_target_anim"));
    FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>> Bindings = Read.Data->GetArrayField(TEXT("bindings"));
    TestEqual(TEXT("1 binding returned"), Bindings.Num(), 1);
    if (Bindings.Num() > 0)
    {
        TestFalse(TEXT("target_exists is false"), Bindings[0]->AsObject()->GetBoolField(TEXT("target_exists")));
    }

    // Apply removal with dry_run = false
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetStringField(TEXT("animation_name"), TEXT("dangling_target_anim"));
    Params->SetBoolField(TEXT("dry_run"), false);

    FCortexCommandResult Result = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), Params);
    TestTrue(TEXT("Dangling target binding removal succeeds"), Result.bSuccess);
    if (Result.bSuccess && Result.Data.IsValid())
    {
        TestTrue(TEXT("changed is true"), Result.Data->GetBoolField(TEXT("changed")));
        TestTrue(TEXT("scene_data_removed is true"), Result.Data->GetBoolField(TEXT("scene_data_removed")));
        TestEqual(TEXT("after.umg_binding_count is 0"),
            Result.Data->GetObjectField(TEXT("after"))->GetIntegerField(TEXT("umg_binding_count")), 0);
        TestEqual(TEXT("after.movie_scene_binding_count is 0"),
            Result.Data->GetObjectField(TEXT("after"))->GetIntegerField(TEXT("movie_scene_binding_count")), 0);
        TestEqual(TEXT("after.track_count is 0"),
            Result.Data->GetObjectField(TEXT("after"))->GetIntegerField(TEXT("track_count")), 0);
        TestNull(TEXT("Possessable removed from MovieScene"), MS->FindPossessable(DanglingGuid));
        TestEqual(TEXT("AnimationBindings array is empty"), Anim->AnimationBindings.Num(), 0);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingPossessableWithChildrenRejectionTest,
    "Cortex.UMG.AnimationBinding.PossessableWithChildrenRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingPossessableWithChildrenRejectionTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    UWidgetAnimation* Anim = nullptr;
    if (WBP && WBP->Animations.Num() > 0)
    {
        Anim = WBP->Animations[0];
    }
    UMovieScene* MS = Anim ? Anim->MovieScene : nullptr;
    if (!Anim || !MS)
    {
        AddError(TEXT("Invalid fixture animation"));
        return false;
    }

    // Add a child possessable to Guid1
    FGuid ChildGuid = MS->AddPossessable(TEXT("ChildOfBodySizeBox"), UUserWidget::StaticClass());
    FMovieScenePossessable* ChildPossessable = MS->FindPossessable(ChildGuid);
    if (ChildPossessable)
    {
        ChildPossessable->SetParent(Anim->AnimationBindings[0].AnimationGuid, MS);
    }

    TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
    InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    InspectParams->SetStringField(TEXT("animation_name"), Anim->GetName());
    FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);

    FCortexCommandResult Result = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), Params);
    TestFalse(TEXT("Removal rejected due to child possessables"), Result.bSuccess);
    TestEqual(TEXT("Error is ANIMATION_BINDING_UNSUPPORTED"),
        Result.ErrorCode, CortexErrorCodes::AnimationBindingUnsupported);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingUnsupportedChannelTypeRejectionTest,
    "Cortex.UMG.AnimationBinding.UnsupportedChannelTypeRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingUnsupportedChannelTypeRejectionTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    UWidgetAnimation* Anim = nullptr;
    if (WBP && WBP->Animations.Num() > 0)
    {
        Anim = WBP->Animations[0];
    }
    UMovieScene* MS = Anim ? Anim->MovieScene : nullptr;
    if (!Anim || !MS)
    {
        AddError(TEXT("Invalid fixture animation"));
        return false;
    }

    // Add a sub track on the binding Guid1 (Sub track sections are not in the supported set)
    UMovieSceneSubTrack* SubTrack = MS->AddTrack<UMovieSceneSubTrack>(Anim->AnimationBindings[0].AnimationGuid);
    if (SubTrack)
    {
        UMovieSceneSection* Sec = SubTrack->CreateNewSection();
        if (Sec)
        {
            SubTrack->AddSection(*Sec);
            Sec->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(12000)));
        }
    }

    TSharedPtr<FJsonObject> InspectParams = MakeShared<FJsonObject>();
    InspectParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    InspectParams->SetStringField(TEXT("animation_name"), Anim->GetName());
    FCortexCommandResult Read = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), InspectParams);
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);

    FCortexCommandResult Result = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), Params);
    TestFalse(TEXT("Removal rejected due to unsupported channel type"), Result.bSuccess);
    TestEqual(TEXT("Error is ANIMATION_BINDING_UNSUPPORTED"),
        Result.ErrorCode, CortexErrorCodes::AnimationBindingUnsupported);

    return true;
}
