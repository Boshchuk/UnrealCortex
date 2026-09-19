#include "Misc/AutomationTest.h"
#include "Tests/CortexUMGAnimationBindingTestUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingInspectCanonicalTest,
    "Cortex.UMG.AnimationBinding.InspectCanonical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingInspectCanonicalTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    const TArray<uint8> Before = Fixture.CaptureAllAuthoredState();
    const bool bDirtyBefore = Fixture.Blueprint->GetPackage()->IsDirty();
    const FCortexCommandResult Result = Fixture.Router.Execute(
        TEXT("umg.list_animation_bindings"), Fixture.InspectParams());

    TestTrue(TEXT("Canonical inspection succeeds"), Result.bSuccess);
    if (!Result.bSuccess || !Result.Data.IsValid())
    {
        return false;
    }

    TestEqual(TEXT("Three UMG binding records"),
        Result.Data->GetIntegerField(TEXT("umg_binding_count")), 3);
    TestEqual(TEXT("Three MovieScene binding records"),
        Result.Data->GetIntegerField(TEXT("movie_scene_binding_count")), 3);
    TestTrue(TEXT("Four or more property tracks rather than three bindings"),
        Result.Data->GetIntegerField(TEXT("track_count")) >= 4);

    TestTrue(TEXT("Reads do not change authored state"),
        Before == Fixture.CaptureAllAuthoredState());
    TestEqual(TEXT("Reads preserve dirtiness"),
        Fixture.Blueprint->GetPackage()->IsDirty(), bDirtyBefore);

    // Frame rates: independent numerator and denominator
    const TSharedPtr<FJsonObject>* TickRes = nullptr;
    TestTrue(TEXT("tick_resolution present"), Result.Data->TryGetObjectField(TEXT("tick_resolution"), TickRes));
    if (TickRes && TickRes->IsValid())
    {
        TestEqual(TEXT("tick_resolution numerator"), (*TickRes)->GetIntegerField(TEXT("numerator")), 24000);
        TestEqual(TEXT("tick_resolution denominator"), (*TickRes)->GetIntegerField(TEXT("denominator")), 1);
    }

    const TSharedPtr<FJsonObject>* DispRate = nullptr;
    TestTrue(TEXT("display_rate present"), Result.Data->TryGetObjectField(TEXT("display_rate"), DispRate));
    if (DispRate && DispRate->IsValid())
    {
        TestEqual(TEXT("display_rate numerator"), (*DispRate)->GetIntegerField(TEXT("numerator")), 30000);
        TestEqual(TEXT("display_rate denominator"), (*DispRate)->GetIntegerField(TEXT("denominator")), 1001);
    }

    // Playback range: lower/upper bound value and type
    const TSharedPtr<FJsonObject>* PlaybackRange = nullptr;
    TestTrue(TEXT("playback_range present"), Result.Data->TryGetObjectField(TEXT("playback_range"), PlaybackRange));
    if (PlaybackRange && PlaybackRange->IsValid())
    {
        const TSharedPtr<FJsonObject>* LowerBound = nullptr;
        TestTrue(TEXT("lower_bound present"), (*PlaybackRange)->TryGetObjectField(TEXT("lower_bound"), LowerBound));
        if (LowerBound && LowerBound->IsValid())
        {
            TestEqual(TEXT("lower_bound value"), (*LowerBound)->GetIntegerField(TEXT("value")), 120);
            TestEqual(TEXT("lower_bound type"), (*LowerBound)->GetStringField(TEXT("type")), TEXT("Inclusive"));
        }

        const TSharedPtr<FJsonObject>* UpperBound = nullptr;
        TestTrue(TEXT("upper_bound present"), (*PlaybackRange)->TryGetObjectField(TEXT("upper_bound"), UpperBound));
        if (UpperBound && UpperBound->IsValid())
        {
            TestEqual(TEXT("upper_bound value"), (*UpperBound)->GetIntegerField(TEXT("value")), 720);
            TestEqual(TEXT("upper_bound type"), (*UpperBound)->GetStringField(TEXT("type")), TEXT("Exclusive"));
        }

        double LengthSec = 0.0;
        TestTrue(TEXT("length_seconds present"), (*PlaybackRange)->TryGetNumberField(TEXT("length_seconds"), LengthSec));
        TestTrue(TEXT("length_seconds > 0"), LengthSec > 0.0);
    }

    // Exact selector fields on bindings
    const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
    TestTrue(TEXT("bindings array present"), Result.Data->TryGetArrayField(TEXT("bindings"), Bindings));
    if (Bindings)
    {
        TestEqual(TEXT("bindings count matches 3"), Bindings->Num(), 3);
        for (int32 i = 0; i < Bindings->Num(); ++i)
        {
            TSharedPtr<FJsonObject> B = (*Bindings)[i]->AsObject();
            TestTrue(FString::Printf(TEXT("binding[%d] is object"), i), B.IsValid());
            if (!B.IsValid()) continue;

            TestEqual(FString::Printf(TEXT("binding[%d] index"), i), B->GetIntegerField(TEXT("index")), i);
            TestTrue(FString::Printf(TEXT("binding[%d] has valid binding_guid"), i),
                !B->GetStringField(TEXT("binding_guid")).IsEmpty());
            TestTrue(FString::Printf(TEXT("binding[%d] has widget_name"), i),
                B->HasField(TEXT("widget_name")));
            TestTrue(FString::Printf(TEXT("binding[%d] has slot_widget_name"), i),
                B->HasField(TEXT("slot_widget_name")));
            TestTrue(FString::Printf(TEXT("binding[%d] has is_root_widget"), i),
                B->HasField(TEXT("is_root_widget")));
            TestTrue(FString::Printf(TEXT("binding[%d] target_exists"), i),
                B->GetBoolField(TEXT("target_exists")));
            TestEqual(FString::Printf(TEXT("binding[%d] guid_sharing_count"), i),
                B->GetIntegerField(TEXT("guid_sharing_count")), 1);
            TestTrue(FString::Printf(TEXT("binding[%d] possessable_exists"), i),
                B->GetBoolField(TEXT("possessable_exists")));
            TestTrue(FString::Printf(TEXT("binding[%d] has tracks"), i),
                B->GetArrayField(TEXT("tracks")).Num() >= 1);
        }
    }

    // Fingerprint domain_signature verification
    const TSharedPtr<FJsonObject>* Fingerprint = nullptr;
    TestTrue(TEXT("fingerprint present"), Result.Data->TryGetObjectField(TEXT("fingerprint"), Fingerprint));
    if (Fingerprint && Fingerprint->IsValid())
    {
        const TSharedPtr<FJsonObject>* DomainSig = nullptr;
        TestTrue(TEXT("domain_signature present"), (*Fingerprint)->TryGetObjectField(TEXT("domain_signature"), DomainSig));
        if (DomainSig && DomainSig->IsValid())
        {
            TestEqual(TEXT("domain_signature version"), (*DomainSig)->GetIntegerField(TEXT("version")), 1);
            TestEqual(TEXT("domain_signature scope"), (*DomainSig)->GetStringField(TEXT("scope")), TEXT("umg.animation_binding"));
            TestEqual(TEXT("domain_signature animation_name"), (*DomainSig)->GetStringField(TEXT("animation_name")), TEXT("appearance"));
            TestTrue(TEXT("domain_signature digest is non-empty"), !(*DomainSig)->GetStringField(TEXT("digest")).IsEmpty());
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingInspectPaginationTest,
    "Cortex.UMG.AnimationBinding.InspectPagination",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingInspectPaginationTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);

    // Page 1: offset 0, limit 1
    TSharedPtr<FJsonObject> P1 = Fixture.InspectParams();
    P1->SetNumberField(TEXT("offset"), 0);
    P1->SetNumberField(TEXT("limit"), 1);
    FCortexCommandResult R1 = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), P1);
    TestTrue(TEXT("Page 1 succeeds"), R1.bSuccess);
    if (R1.bSuccess && R1.Data.IsValid())
    {
        const TSharedPtr<FJsonObject>* Pag1 = nullptr;
        if (R1.Data->TryGetObjectField(TEXT("pagination"), Pag1) && Pag1 && Pag1->IsValid())
        {
            TestEqual(TEXT("Page 1 total"), (*Pag1)->GetIntegerField(TEXT("total")), 3);
            TestEqual(TEXT("Page 1 offset"), (*Pag1)->GetIntegerField(TEXT("offset")), 0);
            TestEqual(TEXT("Page 1 limit"), (*Pag1)->GetIntegerField(TEXT("limit")), 1);
            TestEqual(TEXT("Page 1 returned"), (*Pag1)->GetIntegerField(TEXT("returned")), 1);
            TestEqual(TEXT("Page 1 next_offset"), (*Pag1)->GetIntegerField(TEXT("next_offset")), 1);
            TestFalse(TEXT("Page 1 is_complete"), (*Pag1)->GetBoolField(TEXT("is_complete")));
        }
        TestEqual(TEXT("Page 1 bindings returned count"), R1.Data->GetArrayField(TEXT("bindings")).Num(), 1);
    }

    // Page 2: offset 2, limit 2
    TSharedPtr<FJsonObject> P2 = Fixture.InspectParams();
    P2->SetNumberField(TEXT("offset"), 2);
    P2->SetNumberField(TEXT("limit"), 2);
    FCortexCommandResult R2 = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), P2);
    TestTrue(TEXT("Page 2 succeeds"), R2.bSuccess);
    if (R2.bSuccess && R2.Data.IsValid())
    {
        const TSharedPtr<FJsonObject>* Pag2 = nullptr;
        if (R2.Data->TryGetObjectField(TEXT("pagination"), Pag2) && Pag2 && Pag2->IsValid())
        {
            TestEqual(TEXT("Page 2 total"), (*Pag2)->GetIntegerField(TEXT("total")), 3);
            TestEqual(TEXT("Page 2 offset"), (*Pag2)->GetIntegerField(TEXT("offset")), 2);
            TestEqual(TEXT("Page 2 returned"), (*Pag2)->GetIntegerField(TEXT("returned")), 1);
            TestTrue(TEXT("Page 2 next_offset is null"), (*Pag2)->HasTypedField<EJson::Null>(TEXT("next_offset")));
            TestTrue(TEXT("Page 2 is_complete"), (*Pag2)->GetBoolField(TEXT("is_complete")));
        }
    }

    // Page 3: offset 5, limit 10 (beyond total)
    TSharedPtr<FJsonObject> P3 = Fixture.InspectParams();
    P3->SetNumberField(TEXT("offset"), 5);
    P3->SetNumberField(TEXT("limit"), 10);
    FCortexCommandResult R3 = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), P3);
    TestTrue(TEXT("Page 3 succeeds"), R3.bSuccess);
    if (R3.bSuccess && R3.Data.IsValid())
    {
        const TSharedPtr<FJsonObject>* Pag3 = nullptr;
        if (R3.Data->TryGetObjectField(TEXT("pagination"), Pag3) && Pag3 && Pag3->IsValid())
        {
            TestEqual(TEXT("Page 3 returned"), (*Pag3)->GetIntegerField(TEXT("returned")), 0);
            TestTrue(TEXT("Page 3 is_complete"), (*Pag3)->GetBoolField(TEXT("is_complete")));
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexUMGAnimationBindingInspectTableCasesTest,
    "Cortex.UMG.AnimationBinding.InspectTableCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexUMGAnimationBindingInspectTableCasesTest::RunTest(const FString& Parameters)
{
    FCortexUMGAnimationBindingFixture Fixture(*this);
    UWidgetBlueprint* WBP = Fixture.Blueprint.Get();

    // Case 1: Null MovieScene
    UWidgetAnimation* NullMSAnim = NewObject<UWidgetAnimation>(WBP, TEXT("NullMSAnim"), RF_Transactional);
    NullMSAnim->MovieScene = nullptr;
    WBP->Animations.Add(NullMSAnim);

    TSharedPtr<FJsonObject> NullMSParams = MakeShared<FJsonObject>();
    NullMSParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    NullMSParams->SetStringField(TEXT("animation_name"), TEXT("NullMSAnim"));
    FCortexCommandResult NullMSResult = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), NullMSParams);
    TestTrue(TEXT("Null MovieScene succeeds with graceful fallback"), NullMSResult.bSuccess);
    if (NullMSResult.bSuccess && NullMSResult.Data.IsValid())
    {
        TestEqual(TEXT("Null MovieScene movie_scene_binding_count is 0"),
            NullMSResult.Data->GetIntegerField(TEXT("movie_scene_binding_count")), 0);
        TestEqual(TEXT("Null MovieScene track_count is 0"),
            NullMSResult.Data->GetIntegerField(TEXT("track_count")), 0);
        TestTrue(TEXT("Null MovieScene produces diagnostic"),
            NullMSResult.Data->GetArrayField(TEXT("diagnostics")).Num() > 0);
    }

    // Case 2: Absent target widget and slot
    UWidgetAnimation* TableAnim = NewObject<UWidgetAnimation>(WBP, TEXT("TableAnim"), RF_Transactional);
    UMovieScene* TableMS = NewObject<UMovieScene>(TableAnim, TEXT("TableAnim"));
    TableAnim->MovieScene = TableMS;

    FGuid DanglingGuid = FGuid::NewGuid();
    FWidgetAnimationBinding DanglingBinding;
    DanglingBinding.WidgetName = TEXT("NonExistentWidget");
    DanglingBinding.SlotWidgetName = TEXT("NonExistentSlot");
    DanglingBinding.AnimationGuid = DanglingGuid;
    DanglingBinding.bIsRootWidget = false;
    TableAnim->AnimationBindings.Add(DanglingBinding);

    // Case 3: Scene-only binding (in MovieScene but not in AnimationBindings)
    FGuid SceneOnlyGuid = TableMS->AddPossessable(TEXT("SceneOnlyWidget"), USizeBox::StaticClass());

    // Case 4: Identical duplicate records
    FGuid DupGuid = TableMS->AddPossessable(TEXT("BodySizeBox"), USizeBox::StaticClass());
    FWidgetAnimationBinding DupBinding;
    DupBinding.WidgetName = TEXT("BodySizeBox");
    DupBinding.SlotWidgetName = NAME_None;
    DupBinding.AnimationGuid = DupGuid;
    DupBinding.bIsRootWidget = false;
    TableAnim->AnimationBindings.Add(DupBinding);
    TableAnim->AnimationBindings.Add(DupBinding); // identical duplicate

    // Case 5: Shared GUID (two distinct bindings sharing the same GUID)
    FGuid SharedGuid = TableMS->AddPossessable(TEXT("BorderBody"), UBorder::StaticClass());
    FWidgetAnimationBinding SharedB1;
    SharedB1.WidgetName = TEXT("BorderBody");
    SharedB1.SlotWidgetName = NAME_None;
    SharedB1.AnimationGuid = SharedGuid;
    SharedB1.bIsRootWidget = false;
    FWidgetAnimationBinding SharedB2;
    SharedB2.WidgetName = TEXT("BorderBody");
    SharedB2.SlotWidgetName = TEXT("SlotB");
    SharedB2.AnimationGuid = SharedGuid;
    SharedB2.bIsRootWidget = false;
    TableAnim->AnimationBindings.Add(SharedB1);
    TableAnim->AnimationBindings.Add(SharedB2);

    // Case 6: Master/event tracks on TableMS
    TableMS->AddTrack<UMovieSceneEventTrack>();

    WBP->Animations.Add(TableAnim);

    TSharedPtr<FJsonObject> TableParams = MakeShared<FJsonObject>();
    TableParams->SetStringField(TEXT("asset_path"), WBP->GetPathName());
    TableParams->SetStringField(TEXT("animation_name"), TEXT("TableAnim"));
    FCortexCommandResult TableResult = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), TableParams);
    TestTrue(TEXT("Table cases inspection succeeds"), TableResult.bSuccess);
    if (TableResult.bSuccess && TableResult.Data.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
        TableResult.Data->TryGetArrayField(TEXT("bindings"), Bindings);
        TestNotNull(TEXT("TableAnim bindings present"), Bindings);
        if (Bindings && Bindings->Num() >= 5)
        {
            // Dangling binding check
            TSharedPtr<FJsonObject> DanglingObj = (*Bindings)[0]->AsObject();
            TestFalse(TEXT("Dangling target_exists is false"), DanglingObj->GetBoolField(TEXT("target_exists")));
            TestFalse(TEXT("Dangling slot_exists is false"), DanglingObj->GetBoolField(TEXT("slot_exists")));
            TestFalse(TEXT("Dangling possessable_exists is false"), DanglingObj->GetBoolField(TEXT("possessable_exists")));

            // Shared GUID check
            TSharedPtr<FJsonObject> SharedObj1 = (*Bindings)[3]->AsObject();
            TSharedPtr<FJsonObject> SharedObj2 = (*Bindings)[4]->AsObject();
            TestEqual(TEXT("Shared GUID sharing count 1"), SharedObj1->GetIntegerField(TEXT("guid_sharing_count")), 2);
            TestEqual(TEXT("Shared GUID sharing count 2"), SharedObj2->GetIntegerField(TEXT("guid_sharing_count")), 2);
        }

        // Diagnostics check: should diagnose dangling target, missing possessable, scene-only binding, duplicate records, shared GUID, and master track
        const TArray<TSharedPtr<FJsonValue>>* Diags = nullptr;
        TableResult.Data->TryGetArrayField(TEXT("diagnostics"), Diags);
        TestNotNull(TEXT("diagnostics present"), Diags);
        if (Diags)
        {
            TestTrue(TEXT("Diagnostics array has entries for edge cases"), Diags->Num() >= 4);
        }
    }

    // Case 7: Missing WidgetTree retains existing loader error
    UPackage* NoTreePackage = CreatePackage(TEXT("/Temp/CortexUMGNoTreeTest"));
    UWidgetBlueprint* NoTreeWBP = NewObject<UWidgetBlueprint>(
        NoTreePackage, TEXT("WBP_NoTree"), RF_Public | RF_Standalone | RF_Transactional);
    NoTreeWBP->ParentClass = UUserWidget::StaticClass();
    NoTreeWBP->WidgetTree = nullptr; // deliberately null

    TSharedPtr<FJsonObject> NoTreeParams = MakeShared<FJsonObject>();
    NoTreeParams->SetStringField(TEXT("asset_path"), NoTreeWBP->GetPathName());
    NoTreeParams->SetStringField(TEXT("animation_name"), TEXT("appearance"));
    FCortexCommandResult NoTreeResult = Fixture.Router.Execute(TEXT("umg.list_animation_bindings"), NoTreeParams);
    TestFalse(TEXT("Missing WidgetTree fails"), NoTreeResult.bSuccess);
    TestEqual(TEXT("Missing WidgetTree returns BLUEPRINT_NOT_FOUND"),
        NoTreeResult.ErrorCode, CortexErrorCodes::BlueprintNotFound);

    NoTreeWBP->MarkAsGarbage();
    return true;
}
