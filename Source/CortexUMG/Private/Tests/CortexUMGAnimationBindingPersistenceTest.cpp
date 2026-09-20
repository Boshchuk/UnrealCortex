#include "Misc/AutomationTest.h"
#include "Tests/CortexUMGAnimationBindingTestUtils.h"
#include "Operations/CortexUMGAnimationBindingUtils.h"
#include "Editor.h"
#include "PackageTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/SavePackage.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

struct FCortexUMGAnimationBindingPersistenceFixture
{
    explicit FCortexUMGAnimationBindingPersistenceFixture(FAutomationTestBase& Test)
        : TestRef(Test)
    {
        const FString UniqueId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensInBraces);
        PackageName = FString::Printf(TEXT("/Game/Temp/CortexUMGAnimationBindingTests/WBP_PersistenceTest_%s"), *UniqueId);
        DiskFilename = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

        // Refuse to overwrite an unrelated pre-existing package
        if (IFileManager::Get().FileExists(*DiskFilename) || FPackageName::DoesPackageExist(PackageName))
        {
            TestRef.AddError(FString::Printf(TEXT("Refusing to overwrite existing package: %s"), *PackageName));
            bSetupValid = false;
            return;
        }

        IFileManager::Get().MakeDirectory(*FPaths::GetPath(DiskFilename), true);

        UPackage* TestPackage = CreatePackage(*PackageName);
        if (!TestPackage)
        {
            TestRef.AddError(FString::Printf(TEXT("Failed to create package: %s"), *PackageName));
            bSetupValid = false;
            return;
        }

        const FString AssetName = FString::Printf(TEXT("WBP_PersistenceTest_%s"), *UniqueId);
        UWidgetBlueprint* WBP = CortexUMGAnimationBindingTestUtils::CreateAnimationBindingWidgetBlueprint(
            TestPackage, AssetName);

        // Initial save to disk so there is an existing package file
        FSavePackageArgs SaveArgs;
        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
        SaveArgs.SaveFlags = SAVE_NoError;
        const bool bSaved = UPackage::SavePackage(TestPackage, WBP, *DiskFilename, SaveArgs);
        if (!bSaved)
        {
            TestRef.AddError(FString::Printf(TEXT("Failed to save initial test package to disk: %s"), *DiskFilename));
            bSetupValid = false;
            return;
        }

        TestPackage->ClearDirtyFlag();
        Blueprint = TStrongObjectPtr<UWidgetBlueprint>(WBP);

        Router.RegisterDomain(TEXT("umg"), TEXT("Cortex UMG"), TEXT("1.0.1"),
            MakeShared<FCortexUMGCommandHandler>());

        bSetupValid = true;
    }

    ~FCortexUMGAnimationBindingPersistenceFixture()
    {
        #if WITH_DEV_AUTOMATION_TESTS
        CortexUMGAnimationBindingUtils::SetFailureInjection(
            CortexUMGAnimationBindingUtils::EFailureInjection::None);
        #endif

        if (Blueprint.IsValid())
        {
            UWidgetBlueprint* WBP = Blueprint.Get();
            UPackage* Pkg = WBP->GetPackage();
            if (Pkg)
            {
                ResetLoaders(Pkg);
                Pkg->ClearDirtyFlag();
            }
            WBP->MarkAsGarbage();
            Blueprint.Reset();
        }

        if (IFileManager::Get().FileExists(*DiskFilename))
        {
            IFileManager::Get().Delete(*DiskFilename);
        }
    }

    bool IsValid() const { return bSetupValid; }

    FString GetDiskFileHash() const
    {
        TArray<uint8> FileBytes;
        if (FFileHelper::LoadFileToArray(FileBytes, *DiskFilename))
        {
            return FMD5::HashBytes(FileBytes.GetData(), FileBytes.Num());
        }
        return FString();
    }

    TSharedPtr<FJsonObject> InspectParams() const
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        if (Blueprint.IsValid())
        {
            Params->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
        }
        Params->SetStringField(TEXT("animation_name"), TEXT("appearance"));
        return Params;
    }

    TSharedPtr<FJsonObject> RemovalParams(
        const TSharedPtr<FJsonObject>& Inspection, int32 BindingIndex) const
    {
        TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
        if (Blueprint.IsValid())
        {
            Params->SetStringField(TEXT("asset_path"), Blueprint->GetPathName());
        }
        Params->SetStringField(TEXT("animation_name"), TEXT("appearance"));

        if (Inspection.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* BindingsArray = nullptr;
            if (Inspection->TryGetArrayField(TEXT("bindings"), BindingsArray) && BindingsArray && BindingsArray->IsValidIndex(BindingIndex))
            {
                TSharedPtr<FJsonObject> BindingObj = (*BindingsArray)[BindingIndex]->AsObject();
                if (BindingObj.IsValid())
                {
                    TSharedPtr<FJsonObject> Selector = MakeShared<FJsonObject>();
                    Selector->SetStringField(TEXT("binding_guid"), BindingObj->GetStringField(TEXT("binding_guid")));
                    Selector->SetStringField(TEXT("widget_name"), BindingObj->GetStringField(TEXT("widget_name")));
                    Selector->SetStringField(TEXT("slot_widget_name"), BindingObj->GetStringField(TEXT("slot_widget_name")));
                    Selector->SetBoolField(TEXT("is_root_widget"), BindingObj->GetBoolField(TEXT("is_root_widget")));
                    Params->SetObjectField(TEXT("selector"), Selector);
                }
            }

            const TSharedPtr<FJsonObject>* FingerprintObj = nullptr;
            if (Inspection->TryGetObjectField(TEXT("fingerprint"), FingerprintObj) && FingerprintObj && FingerprintObj->IsValid())
            {
                Params->SetObjectField(TEXT("expected_fingerprint"), *FingerprintObj);
            }
        }

        return Params;
    }

    TArray<uint8> CaptureRetainedAuthoredState(const FGuid& RemovedGuid) const
    {
        TArray<uint8> Buffer;
        FMemoryWriter Ar(Buffer);
        if (Blueprint.IsValid())
        {
            for (UWidgetAnimation* Anim : Blueprint->Animations)
            {
                CortexUMGAnimationBindingTestUtils::SerializeAnimationAuthoredData(Anim, Ar, &RemovedGuid);
            }
        }
        return Buffer;
    }

    FString PackageName;
    FString DiskFilename;
    TStrongObjectPtr<UWidgetBlueprint> Blueprint;
    FCortexCommandRouter Router;
    FAutomationTestBase& TestRef;
    bool bSetupValid = false;
};

// -----------------------------------------------------------------------------
// Step 1: Preview Default (save=false, dry_run=true)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingPersistencePreviewDefaultTest,
    "Cortex.UMG.AnimationBinding.PersistencePreviewDefault",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingPersistencePreviewDefaultTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    const FString DiskHashBefore = Fixture.GetDiskFileHash();
    TestFalse(TEXT("Disk file exists and hash is non-empty"), DiskHashBefore.IsEmpty());
    TestFalse(TEXT("Package starts clean"), Fixture.Blueprint->GetPackage()->IsDirty());

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), true);
    Params->SetBoolField(TEXT("save"), false);

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Preview succeeds"), Result.bSuccess);
    if (Result.bSuccess && Result.Data.IsValid())
    {
        TestTrue(TEXT("dry_run is true"), Result.Data->GetBoolField(TEXT("dry_run")));
        TestFalse(TEXT("changed is false in preview"), Result.Data->GetBoolField(TEXT("changed")));
        TestFalse(TEXT("save_attempted is false"), Result.Data->GetBoolField(TEXT("save_attempted")));
        TestFalse(TEXT("saved is false"), Result.Data->GetBoolField(TEXT("saved")));
        TestTrue(TEXT("save_error is null"), Result.Data->HasTypedField<EJson::Null>(TEXT("save_error")));
    }

    TestFalse(TEXT("Package remains clean"), Fixture.Blueprint->GetPackage()->IsDirty());
    TestEqual(TEXT("Disk file hash unchanged"), Fixture.GetDiskFileHash(), DiskHashBefore);

    return true;
}

// -----------------------------------------------------------------------------
// Step 1: Preview With Save Conflict (dry_run=true, save=true -> INVALID_FIELD)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingPersistencePreviewSaveConflictTest,
    "Cortex.UMG.AnimationBinding.PersistencePreviewSaveConflict",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingPersistencePreviewSaveConflictTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    const FString DiskHashBefore = Fixture.GetDiskFileHash();
    TestFalse(TEXT("Disk file exists and hash is non-empty"), DiskHashBefore.IsEmpty());
    TestFalse(TEXT("Package starts clean"), Fixture.Blueprint->GetPackage()->IsDirty());

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), true);
    Params->SetBoolField(TEXT("save"), true);

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestFalse(TEXT("Conflict dry_run=true and save=true is rejected"), Result.bSuccess);
    TestEqual(TEXT("Error code is INVALID_FIELD"), Result.ErrorCode, CortexErrorCodes::InvalidField);
    TestFalse(TEXT("Package remains clean"), Fixture.Blueprint->GetPackage()->IsDirty());
    TestEqual(TEXT("Disk file hash unchanged"), Fixture.GetDiskFileHash(), DiskHashBefore);

    return true;
}

// -----------------------------------------------------------------------------
// Step 1: Apply Without Save (dry_run=false, save=false)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingSaveOutcomeSeparateTest,
    "Cortex.UMG.AnimationBinding.SaveOutcomeSeparate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingSaveOutcomeSeparateTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    const FString DiskHashBefore = Fixture.GetDiskFileHash();
    TestFalse(TEXT("Disk file exists and hash is non-empty"), DiskHashBefore.IsEmpty());
    TestFalse(TEXT("Package starts clean"), Fixture.Blueprint->GetPackage()->IsDirty());

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);
    Params->SetBoolField(TEXT("save"), false);

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Apply without save succeeds"), Result.bSuccess);
    if (Result.bSuccess && Result.Data.IsValid())
    {
        TestFalse(TEXT("dry_run is false"), Result.Data->GetBoolField(TEXT("dry_run")));
        TestTrue(TEXT("changed is true"), Result.Data->GetBoolField(TEXT("changed")));
        TestFalse(TEXT("save_attempted is false"), Result.Data->GetBoolField(TEXT("save_attempted")));
        TestFalse(TEXT("saved is false"), Result.Data->GetBoolField(TEXT("saved")));
        TestTrue(TEXT("save_error is null"), Result.Data->HasTypedField<EJson::Null>(TEXT("save_error")));
    }

    TestTrue(TEXT("Package is marked dirty in memory"), Fixture.Blueprint->GetPackage()->IsDirty());
    TestEqual(TEXT("Disk file is unchanged"), Fixture.GetDiskFileHash(), DiskHashBefore);

    return true;
}

// -----------------------------------------------------------------------------
// Step 1: Apply With Successful Save (dry_run=false, save=true)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingSaveSuccessTest,
    "Cortex.UMG.AnimationBinding.SaveSuccess",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingSaveSuccessTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    const FString DiskHashBefore = Fixture.GetDiskFileHash();
    TestFalse(TEXT("Disk file exists and hash is non-empty"), DiskHashBefore.IsEmpty());
    TestFalse(TEXT("Package starts clean"), Fixture.Blueprint->GetPackage()->IsDirty());

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);
    Params->SetBoolField(TEXT("save"), true);

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    TestTrue(TEXT("Apply with save succeeds"), Result.bSuccess);
    if (Result.bSuccess && Result.Data.IsValid())
    {
        TestFalse(TEXT("dry_run is false"), Result.Data->GetBoolField(TEXT("dry_run")));
        TestTrue(TEXT("changed is true"), Result.Data->GetBoolField(TEXT("changed")));
        TestTrue(TEXT("save_attempted is true"), Result.Data->GetBoolField(TEXT("save_attempted")));
        TestTrue(TEXT("saved is true"), Result.Data->GetBoolField(TEXT("saved")));
        TestTrue(TEXT("save_error is null"), Result.Data->HasTypedField<EJson::Null>(TEXT("save_error")));

        const TSharedPtr<FJsonObject>* FingerprintObj = nullptr;
        TestTrue(TEXT("Fingerprint present"), Result.Data->TryGetObjectField(TEXT("fingerprint"), FingerprintObj));
        if (FingerprintObj && FingerprintObj->IsValid())
        {
            TestFalse(TEXT("Fingerprint is_dirty is false after save"),
                (*FingerprintObj)->GetBoolField(TEXT("is_dirty")));
        }
    }

    TestFalse(TEXT("Package dirty flag is cleared after successful save"),
        Fixture.Blueprint->GetPackage()->IsDirty());
    const FString DiskHashAfter = Fixture.GetDiskFileHash();
    TestFalse(TEXT("Disk file updated (hash changed)"), DiskHashAfter == DiskHashBefore);

    return true;
}

// -----------------------------------------------------------------------------
// Step 2: Apply With Injected Save Failure
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingSaveFailureReportTest,
    "Cortex.UMG.AnimationBinding.SaveFailureReport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingSaveFailureReportTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    const FString DiskHashBefore = Fixture.GetDiskFileHash();
    TestFalse(TEXT("Disk file exists and hash is non-empty"), DiskHashBefore.IsEmpty());
    TestFalse(TEXT("Package starts clean"), Fixture.Blueprint->GetPackage()->IsDirty());

    #if WITH_DEV_AUTOMATION_TESTS
    CortexUMGAnimationBindingUtils::SetFailureInjection(
        CortexUMGAnimationBindingUtils::EFailureInjection::FailSavePackage);
    #endif

    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);
    Params->SetBoolField(TEXT("save"), true);

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);

    // Save failure must report mutation success (changed=true) but saved=false
    TestTrue(TEXT("Operation succeeds overall because in-memory mutation succeeded"), Result.bSuccess);
    if (Result.bSuccess && Result.Data.IsValid())
    {
        TestFalse(TEXT("dry_run is false"), Result.Data->GetBoolField(TEXT("dry_run")));
        TestTrue(TEXT("changed is true"), Result.Data->GetBoolField(TEXT("changed")));
        TestTrue(TEXT("save_attempted is true"), Result.Data->GetBoolField(TEXT("save_attempted")));
        TestFalse(TEXT("saved is false on failed save"), Result.Data->GetBoolField(TEXT("saved")));
        TestFalse(TEXT("save_error is not null"), Result.Data->HasTypedField<EJson::Null>(TEXT("save_error")));
        TestTrue(TEXT("save_error is string"), Result.Data->HasTypedField<EJson::String>(TEXT("save_error")));
        if (Result.Data->HasTypedField<EJson::String>(TEXT("save_error")))
        {
            TestFalse(TEXT("save_error message is not empty"),
                Result.Data->GetStringField(TEXT("save_error")).IsEmpty());
        }

        const TSharedPtr<FJsonObject>* FingerprintObj = nullptr;
        TestTrue(TEXT("Fingerprint present in response"), Result.Data->TryGetObjectField(TEXT("fingerprint"), FingerprintObj));
        if (FingerprintObj && FingerprintObj->IsValid())
        {
            TestTrue(TEXT("Refreshed fingerprint reports is_dirty == true"),
                (*FingerprintObj)->GetBoolField(TEXT("is_dirty")));
        }
    }

    // In-memory removal remains applied: do NOT roll back!
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
    if (Anim)
    {
        TestEqual(TEXT("AnimationBindings count is 2 (mutation remained applied)"),
            Anim->AnimationBindings.Num(), 2);
    }

    // Package is dirty in memory, but disk file stays at pre-call revision
    TestTrue(TEXT("Package remains dirty in memory"), Fixture.Blueprint->GetPackage()->IsDirty());
    TestEqual(TEXT("Disk file content stays at pre-call revision"),
        Fixture.GetDiskFileHash(), DiskHashBefore);

    // Repeated call with old selector/token must not remove anything else (fails with STALE_PRECONDITION)
    const FCortexCommandResult RepeatResult = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);
    TestFalse(TEXT("Repeated call with old token is rejected"), RepeatResult.bSuccess);
    TestEqual(TEXT("Repeated call fails with STALE_PRECONDITION"),
        RepeatResult.ErrorCode, CortexErrorCodes::StalePrecondition);
    if (Anim)
    {
        TestEqual(TEXT("AnimationBindings count still 2 after rejected repeated call"),
            Anim->AnimationBindings.Num(), 2);
    }

    #if WITH_DEV_AUTOMATION_TESTS
    CortexUMGAnimationBindingUtils::SetFailureInjection(
        CortexUMGAnimationBindingUtils::EFailureInjection::None);
    #endif

    return true;
}

// -----------------------------------------------------------------------------
// Step 5: Reload and Compile the Saved Duplicate
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingReloadAndCompileTest,
    "Cortex.UMG.AnimationBinding.ReloadAndCompile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingReloadAndCompileTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    // Capture the GUID of the binding to remove
    const TArray<TSharedPtr<FJsonValue>>* BindingsArray = nullptr;
    Read.Data->TryGetArrayField(TEXT("bindings"), BindingsArray);
    TestNotNull(TEXT("Bindings array present"), BindingsArray);
    if (!BindingsArray || BindingsArray->Num() == 0)
    {
        return false;
    }

    FGuid RemovedGuid;
    FGuid::Parse((*BindingsArray)[0]->AsObject()->GetStringField(TEXT("binding_guid")), RemovedGuid);
    const TArray<uint8> RetainedStateExpected = Fixture.CaptureRetainedAuthoredState(RemovedGuid);

    // Apply removal with save = true
    TSharedPtr<FJsonObject> Params = Fixture.RemovalParams(Read.Data, 0);
    Params->SetBoolField(TEXT("dry_run"), false);
    Params->SetBoolField(TEXT("save"), true);

    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), Params);
    TestTrue(TEXT("Removal with save succeeds"), Result.bSuccess);
    if (!Result.bSuccess || !Result.Data.IsValid())
    {
        return false;
    }
    TestTrue(TEXT("Saved is true"), Result.Data->GetBoolField(TEXT("saved")));

    // Release test-owned references before reload
    const FString PackagePath = Fixture.PackageName;
    const FString AssetPath = Fixture.Blueprint->GetPathName();
    UPackage* Package = Fixture.Blueprint->GetPackage();
    Fixture.Blueprint.Reset();

    if (Package)
    {
        ResetLoaders(Package);
        Package->ClearDirtyFlag();
    }

    // Reload fresh from disk via LoadObject
    UWidgetBlueprint* ReloadedBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
    TestNotNull(TEXT("Reloaded WidgetBlueprint exists"), ReloadedBP);
    if (!ReloadedBP)
    {
        return false;
    }
    // Re-anchor to fixture so destructor cleans it up
    Fixture.Blueprint = TStrongObjectPtr<UWidgetBlueprint>(ReloadedBP);

    // Verify retained state after reload from disk
    UWidgetAnimation* ReloadedAnim = nullptr;
    for (UWidgetAnimation* A : ReloadedBP->Animations)
    {
        if (A && A->GetName() == TEXT("appearance"))
        {
            ReloadedAnim = A;
            break;
        }
    }
    TestNotNull(TEXT("Reloaded appearance animation exists"), ReloadedAnim);
    if (!ReloadedAnim || !ReloadedAnim->MovieScene)
    {
        return false;
    }

    TestEqual(TEXT("Retained UMG bindings count is 2"), ReloadedAnim->AnimationBindings.Num(), 2);
    TestNull(TEXT("Removed possessable does not exist in MovieScene"),
        ReloadedAnim->MovieScene->FindPossessable(RemovedGuid));

    // Compare retained authored state byte-for-byte with pre-mutation capture
    const TArray<uint8> RetainedStateActual = Fixture.CaptureRetainedAuthoredState(RemovedGuid);
    TestTrue(TEXT("Retained authored state after reload is identical to pre-mutation expectation"),
        RetainedStateActual == RetainedStateExpected);

    // Explicitly compile the test Blueprint as a separate authorized test action
    FKismetEditorUtilities::CompileBlueprint(ReloadedBP);
    TestEqual(TEXT("Zero compilation errors after compile"), ReloadedBP->Status, BS_UpToDate);

    // Verify no new dangling-target diagnostics after compile
    const FCortexCommandResult PostCompileInspect = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Post-compile inspect succeeds"), PostCompileInspect.bSuccess);
    if (PostCompileInspect.bSuccess && PostCompileInspect.Data.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Diagnostics = nullptr;
        if (PostCompileInspect.Data->TryGetArrayField(TEXT("diagnostics"), Diagnostics) && Diagnostics)
        {
            for (const TSharedPtr<FJsonValue>& Diag : *Diagnostics)
            {
                TestFalse(TEXT("No dangling target diagnostics"),
                    Diag->AsString().Contains(TEXT("does not exist in WidgetTree")));
            }
        }
    }

    return true;
}

// -----------------------------------------------------------------------------
// Step 6: Create Integration Seed (/Game/UI/WBP_AnimationBindingFixture)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingCreateIntegrationSeedTest,
    "Cortex.UMG.AnimationBinding.CreateIntegrationSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingCreateIntegrationSeedTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UI/WBP_AnimationBindingFixture");
    const FString DiskFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    if (IFileManager::Get().FileExists(*DiskFilename))
    {
        TestTrue(TEXT("Disk asset file exists"), true);
        return true;
    }

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(DiskFilename), true);

    UPackage* SeedPackage = CreatePackage(*PackageName);
    TestNotNull(TEXT("Seed package created"), SeedPackage);
    if (!SeedPackage)
    {
        return false;
    }

    UWidgetBlueprint* WBP = CortexUMGAnimationBindingTestUtils::CreateAnimationBindingWidgetBlueprint(
        SeedPackage, TEXT("WBP_AnimationBindingFixture"));
    TestNotNull(TEXT("Seed WidgetBlueprint created"), WBP);
    if (!WBP)
    {
        return false;
    }

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(SeedPackage, WBP, *DiskFilename, SaveArgs);
    TestTrue(TEXT("Seed package saved to disk"), bSaved);
    SeedPackage->ClearDirtyFlag();

    TestTrue(TEXT("Disk asset file exists"), IFileManager::Get().FileExists(*DiskFilename));
    return bSaved;
}
