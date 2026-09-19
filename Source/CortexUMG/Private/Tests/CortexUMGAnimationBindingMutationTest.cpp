#include "Misc/AutomationTest.h"
#include "Tests/CortexUMGAnimationBindingTestUtils.h"
#include "Operations/CortexUMGAnimationBindingUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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

    // 8. Explicit apply (dry_run=false) returns unsupported in Task 2
    {
        TSharedPtr<FJsonObject> P = Fixture.RemovalParams(Read.Data, 0);
        P->SetBoolField(TEXT("dry_run"), false);
        FCortexCommandResult R = Fixture.Router.Execute(TEXT("umg.remove_animation_binding"), P);
        TestFalse(TEXT("dry_run=false returns unsupported in Task 2"), R.bSuccess);
        TestEqual(TEXT("ErrorCode is ANIMATION_BINDING_UNSUPPORTED"), R.ErrorCode, CortexErrorCodes::AnimationBindingUnsupported);
        VerifyPreserved();
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
                TestEqual(TEXT("after track_count"), (*AfterObj)->GetIntegerField(TEXT("track_count")), 4);
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
    UMovieScene* RootMS = NewObject<UMovieScene>(RootAnim, TEXT("RootAnim"));
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
