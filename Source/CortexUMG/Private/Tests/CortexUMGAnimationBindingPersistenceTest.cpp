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
    UWidgetBlueprint* OldBP = Fixture.Blueprint.Get();
    UPackage* Package = Fixture.Blueprint->GetPackage();
    Fixture.Blueprint.Reset();

    if (Package)
    {
        TArray<UPackage*> PackagesToUnload;
        PackagesToUnload.Add(Package);
        const bool bUnloaded = UPackageTools::UnloadPackages(PackagesToUnload);
        TestTrue(TEXT("Test package was successfully unloaded"), bUnloaded);
        TestNull(TEXT("Package is no longer in memory"), FindPackage(nullptr, *PackagePath));
    }

    // Reload fresh from disk via LoadObject
    UWidgetBlueprint* ReloadedBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath);
    TestNotNull(TEXT("Reloaded WidgetBlueprint exists"), ReloadedBP);
    if (!ReloadedBP)
    {
        return false;
    }
    TestTrue(TEXT("Reloaded BP is a fresh object instance"), ReloadedBP != OldBP);
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
// Step 6: Canonical Integration Seed Verification and Generation
// -----------------------------------------------------------------------------

static bool ValidateSeedAssetStrict(UWidgetBlueprint* ExistingBP, FString* OutError = nullptr)
{
    if (!ExistingBP)
    {
        if (OutError) *OutError = TEXT("WidgetBlueprint is null");
        return false;
    }
    if (ExistingBP->Animations.Num() < 2)
    {
        if (OutError) *OutError = TEXT("Expected at least 2 animations");
        return false;
    }
    UWidgetAnimation* ExistingAnim = nullptr;
    for (UWidgetAnimation* A : ExistingBP->Animations)
    {
        if (A && A->GetName() == TEXT("appearance"))
        {
            ExistingAnim = A;
            break;
        }
    }
    if (!ExistingAnim || !ExistingAnim->MovieScene)
    {
        if (OutError) *OutError = TEXT("appearance animation or MovieScene is null");
        return false;
    }

    if (ExistingAnim->AnimationBindings.Num() != 3)
    {
        if (OutError) *OutError = FString::Printf(TEXT("Expected 3 bindings, found %d"), ExistingAnim->AnimationBindings.Num());
        return false;
    }

    TMap<FName, FGuid> BindingMap;
    for (const FWidgetAnimationBinding& B : ExistingAnim->AnimationBindings)
    {
        if (!B.AnimationGuid.IsValid())
        {
            if (OutError) *OutError = FString::Printf(TEXT("Binding for %s has invalid GUID"), *B.WidgetName.ToString());
            return false;
        }
        BindingMap.Add(B.WidgetName, B.AnimationGuid);
    }

    if (!BindingMap.Contains(TEXT("BodySizeBox")) || !BindingMap.Contains(TEXT("BorderBody")) || !BindingMap.Contains(TEXT("StorylineIcon")))
    {
        if (OutError) *OutError = TEXT("Missing required widget bindings (BodySizeBox, BorderBody, StorylineIcon)");
        return false;
    }

    const UMovieScene* MS = ExistingAnim->MovieScene;
    if (MS->GetPossessableCount() != 3)
    {
        if (OutError) *OutError = FString::Printf(TEXT("Expected 3 possessables, found %d"), MS->GetPossessableCount());
        return false;
    }

    // Validate exact reflected property tracks on each possessable
    bool bBodySizeBoxValid = false;
    bool bBorderBodyValid = false;
    bool bStorylineIconValid = false;

    for (const FMovieSceneBinding& MSB : MS->GetBindings())
    {
        if (MSB.GetObjectGuid() == BindingMap[TEXT("BodySizeBox")])
        {
            bool bHasWidth = false;
            bool bHasHeight = false;
            for (UMovieSceneTrack* Track : MSB.GetTracks())
            {
                if (UMovieSceneFloatTrack* FT = Cast<UMovieSceneFloatTrack>(Track))
                {
                    if (FT->GetPropertyName() == FName("WidthOverride")) bHasWidth = true;
                    if (FT->GetPropertyName() == FName("HeightOverride")) bHasHeight = true;
                }
            }
            bBodySizeBoxValid = bHasWidth && bHasHeight && (MSB.GetTracks().Num() == 2);
        }
        else if (MSB.GetObjectGuid() == BindingMap[TEXT("BorderBody")])
        {
            bool bHasOpacity = false;
            for (UMovieSceneTrack* Track : MSB.GetTracks())
            {
                if (UMovieSceneFloatTrack* FT = Cast<UMovieSceneFloatTrack>(Track))
                {
                    if (FT->GetPropertyName() == FName("RenderOpacity")) bHasOpacity = true;
                }
            }
            bBorderBodyValid = bHasOpacity && (MSB.GetTracks().Num() == 1);
        }
        else if (MSB.GetObjectGuid() == BindingMap[TEXT("StorylineIcon")])
        {
            bool bHasEnabled = false;
            for (UMovieSceneTrack* Track : MSB.GetTracks())
            {
                if (UMovieSceneBoolTrack* BT = Cast<UMovieSceneBoolTrack>(Track))
                {
                    if (BT->GetPropertyName() == FName("bIsEnabled")) bHasEnabled = true;
                }
            }
            bStorylineIconValid = bHasEnabled && (MSB.GetTracks().Num() == 1);
        }
    }

    if (!bBodySizeBoxValid || !bBorderBodyValid || !bStorylineIconValid)
    {
        if (OutError) *OutError = FString::Printf(
            TEXT("Possessable tracks invalid: BodySizeBox=%d, BorderBody=%d, StorylineIcon=%d"),
            (int32)bBodySizeBoxValid, (int32)bBorderBodyValid, (int32)bStorylineIconValid);
        return false;
    }

    // Master track: exactly 1 UMovieSceneEventTrack
    bool bHasMasterEvent = false;
    for (UMovieSceneTrack* Track : MS->GetTracks())
    {
        if (Track && Track->IsA<UMovieSceneEventTrack>())
        {
            bHasMasterEvent = true;
        }
    }
    if (!bHasMasterEvent)
    {
        if (OutError) *OutError = TEXT("Missing master event track");
        return false;
    }

    // Playback node and node GUIDs
    bool bHasPlaybackNode = false;
    bool bAllNodesHaveGuid = true;
    for (UEdGraph* Graph : ExistingBP->UbergraphPages)
    {
        if (Graph)
        {
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (Node && !Node->NodeGuid.IsValid())
                {
                    bAllNodesHaveGuid = false;
                }
                if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
                {
                    if (CallNode->GetFunctionName() == TEXT("PlayAnimation"))
                    {
                        bHasPlaybackNode = true;
                    }
                }
            }
        }
    }

    if (!bHasPlaybackNode || !bAllNodesHaveGuid)
    {
        if (OutError) *OutError = TEXT("Playback node missing or node GUIDs invalid");
        return false;
    }

    return true;
}

static bool VerifyOrBootstrapSeedAsset(
    const FString& PackageName,
    const FString& AssetName,
    bool bAllowBootstrap,
    FString& OutError,
    bool& bOutDidBootstrap)
{
    bOutDidBootstrap = false;
    OutError.Empty();

    const FString DiskFilename = FPackageName::LongPackageNameToFilename(
        PackageName, FPackageName::GetAssetPackageExtension());

    bool bNeedsGeneration = true;
    FString ValidationError = TEXT("File does not exist");

    if (IFileManager::Get().FileExists(*DiskFilename))
    {
        UPackage* ExistingPkg = LoadPackage(nullptr, *PackageName, LOAD_None);
        UWidgetBlueprint* ExistingBP = ExistingPkg ? FindObject<UWidgetBlueprint>(ExistingPkg, *AssetName) : nullptr;
        if (ValidateSeedAssetStrict(ExistingBP, &ValidationError))
        {
            bNeedsGeneration = false;
        }
    }

    if (!bNeedsGeneration)
    {
        return true;
    }

    if (!bAllowBootstrap)
    {
        OutError = FString::Printf(
            TEXT("Canonical integration seed %s is missing or invalid: %s. Normal test runs are read-only and will not overwrite fixtures. Run with parameter 'Bootstrap' or -BootstrapAnimationFixture to create/update it."),
            *PackageName, *ValidationError);
        return false;
    }

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(DiskFilename), true);

    UPackage* SeedPackage = FindPackage(nullptr, *PackageName);
    if (!SeedPackage)
    {
        SeedPackage = CreatePackage(*PackageName);
    }
    if (!SeedPackage)
    {
        OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackageName);
        return false;
    }

    SeedPackage->MarkAsFullyLoaded();
    ResetLoaders(SeedPackage);

    UWidgetBlueprint* WBP = CortexUMGAnimationBindingTestUtils::CreateAnimationBindingWidgetBlueprint(
        SeedPackage, AssetName);
    if (!WBP)
    {
        OutError = FString::Printf(TEXT("Failed to create WidgetBlueprint: %s"), *AssetName);
        return false;
    }

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    const bool bSaved = UPackage::SavePackage(SeedPackage, WBP, *DiskFilename, SaveArgs);
    if (!bSaved)
    {
        OutError = FString::Printf(TEXT("Failed to save seed package to disk: %s"), *DiskFilename);
        return false;
    }

    SeedPackage->ClearDirtyFlag();
    bOutDidBootstrap = true;
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingCreateIntegrationSeedTest,
    "Cortex.UMG.AnimationBinding.CreateIntegrationSeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingCreateIntegrationSeedTest::RunTest(const FString& Parameters)
{
    const FString PackageName = TEXT("/Game/UI/WBP_AnimationBindingFixture");
    const FString AssetName = TEXT("WBP_AnimationBindingFixture");

    const bool bIsExplicitBootstrap = Parameters.Equals(TEXT("Bootstrap"), ESearchCase::IgnoreCase)
        || FParse::Param(FCommandLine::Get(), TEXT("BootstrapAnimationFixture"))
        || (FPlatformMisc::GetEnvironmentVariable(TEXT("CORTEX_BOOTSTRAP_FIXTURES")) == TEXT("1"));

    FString Error;
    bool bDidBootstrap = false;
    const bool bSuccess = VerifyOrBootstrapSeedAsset(PackageName, AssetName, bIsExplicitBootstrap, Error, bDidBootstrap);

    if (!bSuccess)
    {
        AddError(Error);
        return false;
    }

    TestTrue(TEXT("Seed asset verification/bootstrap succeeded"), bSuccess);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingSeedReadOnlyRegressionTest,
    "Cortex.UMG.AnimationBinding.SeedReadOnlyRegression",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingSeedReadOnlyRegressionTest::RunTest(const FString& Parameters)
{
    // Regression test for UC-8: Verify that normal-mode verification against a mismatching
    // seed is strictly read-only: it detects the mismatch, reports failure, does NOT clear
    // or set dirty flags (tested on both clean and already-dirty fixtures), and does NOT
    // overwrite or touch the disk file.
    const FString TestPkgName = TEXT("/Game/Temp/CortexTest_MismatchSeed");
    const FString AssetName = TEXT("CortexTest_MismatchSeed");
    const FString TestFilename = FPackageName::LongPackageNameToFilename(
        TestPkgName, FPackageName::GetAssetPackageExtension());

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(TestFilename), true);

    // 1. Create a controlled mismatching seed: missing required tracks / wrong property layout
    UPackage* TestPkg = CreatePackage(*TestPkgName);
    TestNotNull(TEXT("Test package created"), TestPkg);
    if (!TestPkg)
    {
        return false;
    }

    UWidgetBlueprint* MismatchWBP = NewObject<UWidgetBlueprint>(
        TestPkg, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
    MismatchWBP->ParentClass = UUserWidget::StaticClass();
    MismatchWBP->WidgetTree = NewObject<UWidgetTree>(MismatchWBP, TEXT("WidgetTree"));
    // Add appearance anim with 0 bindings (deliberate mismatch)
    UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(MismatchWBP, TEXT("appearance"), RF_Transactional);
    Anim->MovieScene = NewObject<UMovieScene>(Anim, TEXT("appearance_MS"));
    MismatchWBP->Animations.Add(Anim);

    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(TestPkg, MismatchWBP, *TestFilename, SaveArgs);
    TestTrue(TEXT("Mismatch seed saved to disk"), bSaved);
    TestPkg->ClearDirtyFlag();

    const FDateTime OrigTimestamp = IFileManager::Get().GetTimeStamp(*TestFilename);
    const int64 OrigFileSize = IFileManager::Get().FileSize(*TestFilename);
    TestTrue(TEXT("Original file exists"), OrigFileSize > 0);

    // 2. Case A: Clean fixture verification
    // Verify that normal-mode verification fails and leaves package CLEAN and disk UNTOUCHED.
    TestFalse(TEXT("Package starts clean in Case A"), TestPkg->IsDirty());
    FString ErrorCaseA;
    bool bDidBootstrapCaseA = false;
    const bool bResultCaseA = VerifyOrBootstrapSeedAsset(
        TestPkgName, AssetName, /*bAllowBootstrap=*/false, ErrorCaseA, bDidBootstrapCaseA);

    TestFalse(TEXT("Normal mode verification correctly detects mismatch (Case A)"), bResultCaseA);
    TestFalse(TEXT("Did not bootstrap in normal mode (Case A)"), bDidBootstrapCaseA);
    TestFalse(TEXT("Validation error is populated (Case A)"), ErrorCaseA.IsEmpty());
    TestFalse(TEXT("Package remains clean in Case A"), TestPkg->IsDirty());
    TestEqual(TEXT("Disk file timestamp unchanged (Case A)"),
        IFileManager::Get().GetTimeStamp(*TestFilename), OrigTimestamp);
    TestEqual(TEXT("Disk file size unchanged (Case A)"),
        IFileManager::Get().FileSize(*TestFilename), OrigFileSize);

    // 3. Case B: Already-dirty fixture verification
    // Verify that normal-mode verification fails and leaves package DIRTY and disk UNTOUCHED.
    TestPkg->SetDirtyFlag(true);
    TestTrue(TEXT("Package starts dirty in Case B"), TestPkg->IsDirty());
    FString ErrorCaseB;
    bool bDidBootstrapCaseB = false;
    const bool bResultCaseB = VerifyOrBootstrapSeedAsset(
        TestPkgName, AssetName, /*bAllowBootstrap=*/false, ErrorCaseB, bDidBootstrapCaseB);

    TestFalse(TEXT("Normal mode verification correctly detects mismatch (Case B)"), bResultCaseB);
    TestFalse(TEXT("Did not bootstrap in normal mode (Case B)"), bDidBootstrapCaseB);
    TestFalse(TEXT("Validation error is populated (Case B)"), ErrorCaseB.IsEmpty());
    TestTrue(TEXT("Package remains dirty in Case B (dirty flag not cleared)"), TestPkg->IsDirty());
    TestEqual(TEXT("Disk file timestamp unchanged (Case B)"),
        IFileManager::Get().GetTimeStamp(*TestFilename), OrigTimestamp);
    TestEqual(TEXT("Disk file size unchanged (Case B)"),
        IFileManager::Get().FileSize(*TestFilename), OrigFileSize);

    // 4. Cleanup test file
    ResetLoaders(TestPkg);
    IFileManager::Get().Delete(*TestFilename);
    TestPkg->ClearDirtyFlag();
    TestPkg->MarkAsGarbage();
    return true;
}


// -----------------------------------------------------------------------------
// Step 7: Native Playback Baseline Evaluation Test
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingPlaybackEvaluationTest,
    "Cortex.UMG.AnimationBinding.PlaybackEvaluation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingPlaybackEvaluationTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    TestNotNull(TEXT("WidgetBlueprint exists"), WBP);
    if (!WBP)
    {
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
    TestNotNull(TEXT("Found appearance animation"), Anim);
    if (!Anim || !Anim->MovieScene)
    {
        return false;
    }

    UMovieScene* MS = Anim->MovieScene;
    TRange<FFrameNumber> Range = MS->GetPlaybackRange();
    TestEqual(TEXT("Playback range start is 120"), Range.GetLowerBoundValue().Value, 120);
    TestEqual(TEXT("Playback range end is 720"), Range.GetUpperBoundValue().Value, 720);

    // Locate tracks
    FMovieSceneFloatChannel* WidthChannel = nullptr;
    FMovieSceneFloatChannel* HeightChannel = nullptr;
    FMovieSceneFloatChannel* OpacityChannel = nullptr;
    FMovieSceneBoolChannel* IsEnabledChannel = nullptr;

    const UMovieScene* ConstMS = MS;
    for (const FMovieSceneBinding& Binding : ConstMS->GetBindings())
    {
        for (UMovieSceneTrack* Track : Binding.GetTracks())
        {
            if (UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track))
            {
                if (FloatTrack->GetPropertyName() == FName("WidthOverride"))
                {
                    if (FloatTrack->GetAllSections().Num() > 0)
                    {
                        if (UMovieSceneFloatSection* Sec = Cast<UMovieSceneFloatSection>(FloatTrack->GetAllSections()[0]))
                        {
                            WidthChannel = &Sec->GetChannel();
                        }
                    }
                }
                else if (FloatTrack->GetPropertyName() == FName("HeightOverride"))
                {
                    if (FloatTrack->GetAllSections().Num() > 0)
                    {
                        if (UMovieSceneFloatSection* Sec = Cast<UMovieSceneFloatSection>(FloatTrack->GetAllSections()[0]))
                        {
                            HeightChannel = &Sec->GetChannel();
                        }
                    }
                }
                else if (FloatTrack->GetPropertyName() == FName("RenderOpacity"))
                {
                    if (FloatTrack->GetAllSections().Num() > 0)
                    {
                        if (UMovieSceneFloatSection* Sec = Cast<UMovieSceneFloatSection>(FloatTrack->GetAllSections()[0]))
                        {
                            OpacityChannel = &Sec->GetChannel();
                        }
                    }
                }
            }
            else if (UMovieSceneBoolTrack* BoolTrack = Cast<UMovieSceneBoolTrack>(Track))
            {
                if (BoolTrack->GetPropertyName() == FName("bIsEnabled"))
                {
                    if (BoolTrack->GetAllSections().Num() > 0)
                    {
                        if (UMovieSceneBoolSection* Sec = Cast<UMovieSceneBoolSection>(BoolTrack->GetAllSections()[0]))
                        {
                            IsEnabledChannel = &Sec->GetChannel();
                        }
                    }
                }
            }
        }
    }

    TestNotNull(TEXT("Found WidthOverride channel"), WidthChannel);
    TestNotNull(TEXT("Found HeightOverride channel"), HeightChannel);
    TestNotNull(TEXT("Found RenderOpacity channel"), OpacityChannel);
    TestNotNull(TEXT("Found bIsEnabled channel"), IsEnabledChannel);
    if (!WidthChannel || !HeightChannel || !OpacityChannel || !IsEnabledChannel)
    {
        return false;
    }

    // Helper evaluation lambda
    auto EvalFloat = [](FMovieSceneFloatChannel* Chan, int32 Frame) -> float
    {
        float Val = 0.0f;
        Chan->Evaluate(FFrameTime(FFrameNumber(Frame)), Val);
        return Val;
    };
    auto EvalBool = [](FMovieSceneBoolChannel* Chan, int32 Frame) -> bool
    {
        bool Val = false;
        Chan->Evaluate(FFrameTime(FFrameNumber(Frame)), Val);
        return Val;
    };

    // Frame 120 (Keyframe 1)
    TestEqual(TEXT("Frame 120: WidthOverride == 100.0"), EvalFloat(WidthChannel, 120), 100.0f);
    TestEqual(TEXT("Frame 120: HeightOverride == 150.0"), EvalFloat(HeightChannel, 120), 150.0f);
    TestEqual(TEXT("Frame 120: RenderOpacity == 0.0"), EvalFloat(OpacityChannel, 120), 0.0f);
    TestEqual(TEXT("Frame 120: bIsEnabled == true"), EvalBool(IsEnabledChannel, 120), true);

    // Frame 180 (Intervening sample between 120 and 240)
    // WidthOverride has cubic interpolation with tangents arrive 1.5, leave 2.0 -> within tolerance 25.0 of 150.0
    const float Width180 = EvalFloat(WidthChannel, 180);
    TestTrue(TEXT("Frame 180: WidthOverride is within cubic tolerance of 150.0"),
        FMath::Abs(Width180 - 150.0f) <= 25.0f);
    // HeightOverride is cubic auto tangent: 150 + 200 * (3*(1/8)^2 - 2*(1/8)^3) = 158.59375
    TestEqual(TEXT("Frame 180: HeightOverride == 158.59375"), EvalFloat(HeightChannel, 180), 158.59375f);
    // RenderOpacity is linear: 0.0 + 1.0 * (60/120) = 0.5
    TestEqual(TEXT("Frame 180: RenderOpacity == 0.5"), EvalFloat(OpacityChannel, 180), 0.5f);
    TestEqual(TEXT("Frame 180: bIsEnabled == true"), EvalBool(IsEnabledChannel, 180), true);

    // Frame 240 (Keyframe 2)
    TestEqual(TEXT("Frame 240: WidthOverride == 200.0"), EvalFloat(WidthChannel, 240), 200.0f);
    // HeightOverride at frame 240: 150 + 200 * (3*(1/4)^2 - 2*(1/4)^3) = 181.25
    TestEqual(TEXT("Frame 240: HeightOverride == 181.25"), EvalFloat(HeightChannel, 240), 181.25f);
    TestEqual(TEXT("Frame 240: RenderOpacity == 1.0"), EvalFloat(OpacityChannel, 240), 1.0f);
    TestEqual(TEXT("Frame 240: bIsEnabled == false"), EvalBool(IsEnabledChannel, 240), false);

    // Frame 420 (Intervening sample between 240 and 600)
    // WidthOverride is linear: 200 + 100 * (180/360) = 250.0
    TestEqual(TEXT("Frame 420: WidthOverride == 250.0"), EvalFloat(WidthChannel, 420), 250.0f);
    // HeightOverride at frame 420: 150 + 200 * (3*(5/8)^2 - 2*(5/8)^3) = 286.71875
    TestEqual(TEXT("Frame 420: HeightOverride == 286.71875"), EvalFloat(HeightChannel, 420), 286.71875f);
    // RenderOpacity is held at 1.0
    TestEqual(TEXT("Frame 420: RenderOpacity == 1.0"), EvalFloat(OpacityChannel, 420), 1.0f);
    TestEqual(TEXT("Frame 420: bIsEnabled == false"), EvalBool(IsEnabledChannel, 420), false);

    // Frame 600 (Keyframe 3)
    TestEqual(TEXT("Frame 600: WidthOverride == 300.0"), EvalFloat(WidthChannel, 600), 300.0f);
    TestEqual(TEXT("Frame 600: HeightOverride == 350.0"), EvalFloat(HeightChannel, 600), 350.0f);
    TestEqual(TEXT("Frame 600: RenderOpacity == 1.0"), EvalFloat(OpacityChannel, 600), 1.0f);
    TestEqual(TEXT("Frame 600: bIsEnabled == false"), EvalBool(IsEnabledChannel, 600), false);

    // Frame 720 (End of playback range)
    TestEqual(TEXT("Frame 720: WidthOverride == 300.0"), EvalFloat(WidthChannel, 720), 300.0f);
    TestEqual(TEXT("Frame 720: HeightOverride == 350.0"), EvalFloat(HeightChannel, 720), 350.0f);
    TestEqual(TEXT("Frame 720: RenderOpacity == 1.0"), EvalFloat(OpacityChannel, 720), 1.0f);
    TestEqual(TEXT("Frame 720: bIsEnabled == false"), EvalBool(IsEnabledChannel, 720), false);

    // Now remove binding 2 (StorylineIcon) and verify retained channels evaluate identically
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    TSharedPtr<FJsonObject> RemoveParams = Fixture.RemovalParams(Read.Data, 2);
    RemoveParams->SetBoolField(TEXT("dry_run"), false);
    RemoveParams->SetBoolField(TEXT("save"), false);

    const FCortexCommandResult RemoveResult = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), RemoveParams);
    TestTrue(TEXT("Removal succeeds"), RemoveResult.bSuccess);

    // Retained channels continue to evaluate to exact baseline values
    TestEqual(TEXT("Post-removal Frame 120: WidthOverride == 100.0"), EvalFloat(WidthChannel, 120), 100.0f);
    TestEqual(TEXT("Post-removal Frame 180: HeightOverride == 158.59375"), EvalFloat(HeightChannel, 180), 158.59375f);
    TestEqual(TEXT("Post-removal Frame 240: RenderOpacity == 1.0"), EvalFloat(OpacityChannel, 240), 1.0f);
    TestEqual(TEXT("Post-removal Frame 600: WidthOverride == 300.0"), EvalFloat(WidthChannel, 600), 300.0f);
    TestEqual(TEXT("Post-removal Frame 600: HeightOverride == 350.0"), EvalFloat(HeightChannel, 600), 350.0f);

    // Negative controls (UC-6):
    // 1. Corrupted float keyframe value fails evaluation check
    const float OrigWidthVal = WidthChannel->GetData().GetValues()[0].Value;
    WidthChannel->GetData().GetValues()[0].Value = 999.0f;
    TestNotEqual(TEXT("Negative control: corrupted WidthOverride key fails baseline comparison"),
        EvalFloat(WidthChannel, 120), 100.0f);
    WidthChannel->GetData().GetValues()[0].Value = OrigWidthVal;

    // 2. Corrupted bool keyframe fails evaluation check
    const bool OrigEnabledVal = IsEnabledChannel->GetData().GetValues()[0];
    IsEnabledChannel->GetData().GetValues()[0] = false;
    TestNotEqual(TEXT("Negative control: corrupted bIsEnabled key fails baseline comparison"),
        EvalBool(IsEnabledChannel, 120), true);
    IsEnabledChannel->GetData().GetValues()[0] = OrigEnabledVal;

    return true;
}

// -----------------------------------------------------------------------------
// Step 8: Real Runtime Playback Acceptance Test (UC-6)
// -----------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingRuntimePlaybackAcceptanceTest,
    "Cortex.UMG.AnimationBinding.RuntimePlaybackAcceptance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingRuntimePlaybackAcceptanceTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingPersistenceFixture Fixture(*this);
    if (!Fixture.IsValid())
    {
        return false;
    }

    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();
    TestNotNull(TEXT("WidgetBlueprint exists"), WBP);
    if (!WBP || !WBP->GeneratedClass)
    {
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
    TestNotNull(TEXT("Found appearance animation"), Anim);
    if (!Anim || !Anim->MovieScene)
    {
        return false;
    }

    struct FCortexUserWidgetTickAccessor : public UUserWidget
    {
        static void TickAnimation(UUserWidget* InWidget, float InDeltaTime)
        {
            if (InWidget)
            {
                static_cast<FCortexUserWidgetTickAccessor*>(InWidget)->TickActionsAndAnimation(InDeltaTime);
                InWidget->FlushAnimations();
            }
        }
    };

    // Instantiate UUserWidget
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const TSubclassOf<UUserWidget> WidgetClass = Cast<UClass>(WBP->GeneratedClass.Get());
    UUserWidget* Widget = World
        ? CreateWidget<UUserWidget>(World, WidgetClass)
        : CreateWidget<UUserWidget>(WBP->WidgetTree.Get(), WidgetClass);
    TestNotNull(TEXT("Live UUserWidget instance created"), Widget);
    if (!Widget)
    {
        return false;
    }

    USizeBox* BodySizeBox = Cast<USizeBox>(Widget->GetWidgetFromName(TEXT("BodySizeBox")));
    UBorder* BorderBody = Cast<UBorder>(Widget->GetWidgetFromName(TEXT("BorderBody")));
    UImage* StorylineIcon = Cast<UImage>(Widget->GetWidgetFromName(TEXT("StorylineIcon")));
    TestNotNull(TEXT("BodySizeBox live component exists"), BodySizeBox);
    TestNotNull(TEXT("BorderBody live component exists"), BorderBody);
    TestNotNull(TEXT("StorylineIcon live component exists"), StorylineIcon);
    if (!BodySizeBox || !BorderBody || !StorylineIcon)
    {
        return false;
    }

    FBoolProperty* EventFiredProp = CastField<FBoolProperty>(
        Widget->GetClass()->FindPropertyByName(TEXT("bAuthoredEventFired")));
    auto WasEventFired = [&]() -> bool
    {
        return EventFiredProp ? EventFiredProp->GetPropertyValue_InContainer(Widget) : false;
    };

    // 1. Play animation forward from start
    const float StartTime = Anim->GetStartTime();
    Widget->PlayAnimation(Anim, StartTime, 1, EUMGSequencePlayMode::Forward, 1.0f);
    FCortexUserWidgetTickAccessor::TickAnimation(Widget, 0.0f);

    // Frame 120 (start key)
    TestEqual(TEXT("Live Frame 120: WidthOverride == 100.0"), BodySizeBox->GetWidthOverride(), 100.0f);
    TestEqual(TEXT("Live Frame 120: HeightOverride == 150.0"), BodySizeBox->GetHeightOverride(), 150.0f);
    TestEqual(TEXT("Live Frame 120: RenderOpacity == 0.0"), BorderBody->GetRenderOpacity(), 0.0f);
    TestEqual(TEXT("Live Frame 120: bIsEnabled == true"), StorylineIcon->GetIsEnabled(), true);
    TestFalse(TEXT("Live Frame 120: Event not yet fired"), WasEventFired());

    // Frame 180 (intervening sample between 120 and 240)
    FCortexUserWidgetTickAccessor::TickAnimation(Widget, 0.0025f);
    TestTrue(TEXT("Live Frame 180: WidthOverride within cubic tolerance of 150.0"),
        FMath::Abs(BodySizeBox->GetWidthOverride() - 150.0f) <= 25.0f);
    TestEqual(TEXT("Live Frame 180: HeightOverride == 158.59375"), BodySizeBox->GetHeightOverride(), 158.59375f);
    TestEqual(TEXT("Live Frame 180: RenderOpacity == 0.5"), BorderBody->GetRenderOpacity(), 0.5f);
    TestEqual(TEXT("Live Frame 180: bIsEnabled == true"), StorylineIcon->GetIsEnabled(), true);
    TestFalse(TEXT("Live Frame 180: Event not yet fired"), WasEventFired());

    // Frame 240 (second key)
    FCortexUserWidgetTickAccessor::TickAnimation(Widget, 0.0025f);
    TestEqual(TEXT("Live Frame 240: WidthOverride == 200.0"), BodySizeBox->GetWidthOverride(), 200.0f);
    TestEqual(TEXT("Live Frame 240: HeightOverride == 181.25"), BodySizeBox->GetHeightOverride(), 181.25f);
    TestEqual(TEXT("Live Frame 240: RenderOpacity == 1.0"), BorderBody->GetRenderOpacity(), 1.0f);
    TestEqual(TEXT("Live Frame 240: bIsEnabled == false"), StorylineIcon->GetIsEnabled(), false);
    TestFalse(TEXT("Live Frame 240: Event not yet fired"), WasEventFired());

    // Frame 360 (authored event callback fires)
    FCortexUserWidgetTickAccessor::TickAnimation(Widget, 0.0050f);
    TestTrue(TEXT("Live Frame 360: Authored event callback fired"), WasEventFired());

    // Frame 420 (intervening sample between 240 and 600)
    FCortexUserWidgetTickAccessor::TickAnimation(Widget, 0.0025f);
    TestEqual(TEXT("Live Frame 420: WidthOverride == 250.0"), BodySizeBox->GetWidthOverride(), 250.0f);
    TestEqual(TEXT("Live Frame 420: HeightOverride == 286.71875"), BodySizeBox->GetHeightOverride(), 286.71875f);
    TestEqual(TEXT("Live Frame 420: RenderOpacity == 1.0"), BorderBody->GetRenderOpacity(), 1.0f);
    TestEqual(TEXT("Live Frame 420: bIsEnabled == false"), StorylineIcon->GetIsEnabled(), false);

    // Frame 600 (third key)
    FCortexUserWidgetTickAccessor::TickAnimation(Widget, 0.0075f);
    TestEqual(TEXT("Live Frame 600: WidthOverride == 300.0"), BodySizeBox->GetWidthOverride(), 300.0f);
    TestEqual(TEXT("Live Frame 600: HeightOverride == 350.0"), BodySizeBox->GetHeightOverride(), 350.0f);
    TestEqual(TEXT("Live Frame 600: RenderOpacity == 1.0"), BorderBody->GetRenderOpacity(), 1.0f);
    TestEqual(TEXT("Live Frame 600: bIsEnabled == false"), StorylineIcon->GetIsEnabled(), false);

    // 2. Remove binding 2 (StorylineIcon), recompile, re-instantiate, verify retained properties evaluate identically
    const FCortexCommandResult Read = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());
    TestTrue(TEXT("Inspect succeeds"), Read.bSuccess);
    if (!Read.bSuccess || !Read.Data.IsValid())
    {
        return false;
    }

    TSharedPtr<FJsonObject> RemoveParams = Fixture.RemovalParams(Read.Data, 2);
    RemoveParams->SetBoolField(TEXT("dry_run"), false);
    RemoveParams->SetBoolField(TEXT("save"), false);

    const FCortexCommandResult RemoveResult = Fixture.Router.Execute(
        TEXT("umg.remove_animation_binding"), RemoveParams);
    TestTrue(TEXT("Removal of StorylineIcon binding succeeds"), RemoveResult.bSuccess);

    FKismetEditorUtilities::CompileBlueprint(WBP);

    UUserWidget* Widget2 = World
        ? CreateWidget<UUserWidget>(World, WidgetClass)
        : CreateWidget<UUserWidget>(WBP->WidgetTree.Get(), WidgetClass);
    TestNotNull(TEXT("Live UUserWidget instance 2 created"), Widget2);
    if (!Widget2)
    {
        return false;
    }

    USizeBox* BodySizeBox2 = Cast<USizeBox>(Widget2->GetWidgetFromName(TEXT("BodySizeBox")));
    UBorder* BorderBody2 = Cast<UBorder>(Widget2->GetWidgetFromName(TEXT("BorderBody")));
    TestNotNull(TEXT("BodySizeBox2 exists"), BodySizeBox2);
    TestNotNull(TEXT("BorderBody2 exists"), BorderBody2);

    Widget2->PlayAnimation(Anim, StartTime, 1, EUMGSequencePlayMode::Forward, 1.0f);
    FCortexUserWidgetTickAccessor::TickAnimation(Widget2, 0.0f);

    TestEqual(TEXT("Post-removal Live Frame 120: WidthOverride == 100.0"), BodySizeBox2->GetWidthOverride(), 100.0f);
    TestEqual(TEXT("Post-removal Live Frame 120: HeightOverride == 150.0"), BodySizeBox2->GetHeightOverride(), 150.0f);
    TestEqual(TEXT("Post-removal Live Frame 120: RenderOpacity == 0.0"), BorderBody2->GetRenderOpacity(), 0.0f);

    FCortexUserWidgetTickAccessor::TickAnimation(Widget2, 0.0025f);
    TestEqual(TEXT("Post-removal Live Frame 180: HeightOverride == 158.59375"), BodySizeBox2->GetHeightOverride(), 158.59375f);
    TestEqual(TEXT("Post-removal Live Frame 180: RenderOpacity == 0.5"), BorderBody2->GetRenderOpacity(), 0.5f);

    FCortexUserWidgetTickAccessor::TickAnimation(Widget2, 0.0025f);
    TestEqual(TEXT("Post-removal Live Frame 240: WidthOverride == 200.0"), BodySizeBox2->GetWidthOverride(), 200.0f);
    TestEqual(TEXT("Post-removal Live Frame 240: RenderOpacity == 1.0"), BorderBody2->GetRenderOpacity(), 1.0f);

    FCortexUserWidgetTickAccessor::TickAnimation(Widget2, 0.0150f);
    TestEqual(TEXT("Post-removal Live Frame 600: WidthOverride == 300.0"), BodySizeBox2->GetWidthOverride(), 300.0f);
    TestEqual(TEXT("Post-removal Live Frame 600: HeightOverride == 350.0"), BodySizeBox2->GetHeightOverride(), 350.0f);

    // 3. Negative controls (UC-6):
    // a. Disabled playback: widget instantiated without PlayAnimation does not change properties
    UUserWidget* UnplayedWidget = World
        ? CreateWidget<UUserWidget>(World, WidgetClass)
        : CreateWidget<UUserWidget>(WBP->WidgetTree.Get(), WidgetClass);
    if (UnplayedWidget)
    {
        FCortexUserWidgetTickAccessor::TickAnimation(UnplayedWidget, 0.05f);
        USizeBox* UnplayedBox = Cast<USizeBox>(UnplayedWidget->GetWidgetFromName(TEXT("BodySizeBox")));
        if (UnplayedBox)
        {
            TestNotEqual(TEXT("Negative control: unplayed widget WidthOverride does not animate to 300.0"),
                UnplayedBox->GetWidthOverride(), 300.0f);
        }
    }

    return true;
}

