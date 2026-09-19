#pragma once

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "CortexCommandRouter.h"
#include "CortexUMGCommandHandler.h"
#include "WidgetBlueprint.h"
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
#include "Serialization/MemoryWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace CortexUMGAnimationBindingTestUtils
{
    inline void SerializeAnimationAuthoredData(
        UWidgetAnimation* Anim,
        FArchive& Ar,
        const FGuid* ExcludedGuid = nullptr)
    {
        if (!Anim)
        {
            return;
        }

        FString AnimName = Anim->GetName();
        Ar << AnimName;

        // Filter and sort UMG bindings
        TArray<FWidgetAnimationBinding> FilteredBindings;
        for (const FWidgetAnimationBinding& Binding : Anim->AnimationBindings)
        {
            if (ExcludedGuid && Binding.AnimationGuid == *ExcludedGuid)
            {
                continue;
            }
            FilteredBindings.Add(Binding);
        }
        FilteredBindings.Sort([](const FWidgetAnimationBinding& A, const FWidgetAnimationBinding& B)
        {
            return A.AnimationGuid < B.AnimationGuid;
        });

        int32 BindingNum = FilteredBindings.Num();
        Ar << BindingNum;
        for (const FWidgetAnimationBinding& Binding : FilteredBindings)
        {
            FString WName = Binding.WidgetName.ToString();
            FString SName = Binding.SlotWidgetName.ToString();
            FGuid AGuid = Binding.AnimationGuid;
            bool bRoot = Binding.bIsRootWidget;
            Ar << WName;
            Ar << SName;
            Ar << AGuid;
            Ar << bRoot;
        }

        if (Anim->MovieScene)
        {
            UMovieScene* MS = Anim->MovieScene;
            TRange<FFrameNumber> Range = MS->GetPlaybackRange();
            bool bLowerOpen = Range.GetLowerBound().IsOpen();
            bool bLowerInc = Range.GetLowerBound().IsInclusive();
            int32 LowerVal = bLowerOpen ? 0 : Range.GetLowerBoundValue().Value;
            bool bUpperOpen = Range.GetUpperBound().IsOpen();
            bool bUpperInc = Range.GetUpperBound().IsInclusive();
            int32 UpperVal = bUpperOpen ? 0 : Range.GetUpperBoundValue().Value;
            Ar << bLowerOpen;
            Ar << bLowerInc;
            Ar << LowerVal;
            Ar << bUpperOpen;
            Ar << bUpperInc;
            Ar << UpperVal;

            int32 TickNum = MS->GetTickResolution().Numerator;
            int32 TickDen = MS->GetTickResolution().Denominator;
            Ar << TickNum;
            Ar << TickDen;

            int32 DispNum = MS->GetDisplayRate().Numerator;
            int32 DispDen = MS->GetDisplayRate().Denominator;
            Ar << DispNum;
            Ar << DispDen;

            TArray<FMovieSceneBinding> FilteredMSBindings;
            const UMovieScene* ConstMS = MS;
            for (const FMovieSceneBinding& MSB : ConstMS->GetBindings())
            {
                if (ExcludedGuid && MSB.GetObjectGuid() == *ExcludedGuid)
                {
                    continue;
                }
                FilteredMSBindings.Add(MSB);
            }
            FilteredMSBindings.Sort([](const FMovieSceneBinding& A, const FMovieSceneBinding& B)
            {
                return A.GetObjectGuid() < B.GetObjectGuid();
            });

            int32 MSBindingNum = FilteredMSBindings.Num();
            Ar << MSBindingNum;
            for (const FMovieSceneBinding& MSB : FilteredMSBindings)
            {
                FGuid ObjGuid = MSB.GetObjectGuid();
                Ar << ObjGuid;

                TArray<UMovieSceneTrack*> SortedTracks = MSB.GetTracks();
                SortedTracks.Sort([](const UMovieSceneTrack& A, const UMovieSceneTrack& B)
                {
                    FString ClassA = A.GetClass()->GetPathName();
                    FString ClassB = B.GetClass()->GetPathName();
                    if (ClassA != ClassB)
                    {
                        return ClassA < ClassB;
                    }
                    return A.GetTrackName().ToString() < B.GetTrackName().ToString();
                });

                int32 TrackNum = SortedTracks.Num();
                Ar << TrackNum;
                for (UMovieSceneTrack* Track : SortedTracks)
                {
                    if (!Track)
                    {
                        continue;
                    }
                    FString TrackClass = Track->GetClass()->GetPathName();
                    FString TrackName = Track->GetTrackName().ToString();
                    Ar << TrackClass;
                    Ar << TrackName;

                    TArray<UMovieSceneSection*> Sections = Track->GetAllSections();
                    int32 SecNum = Sections.Num();
                    Ar << SecNum;
                    for (UMovieSceneSection* Section : Sections)
                    {
                        if (!Section)
                        {
                            continue;
                        }
                        TRange<FFrameNumber> SecRange = Section->GetRange();
                        int32 SecStart = SecRange.GetLowerBound().IsOpen() ? 0 : SecRange.GetLowerBoundValue().Value;
                        int32 SecEnd = SecRange.GetUpperBound().IsOpen() ? 0 : SecRange.GetUpperBoundValue().Value;
                        int32 RowIndex = Section->GetRowIndex();
                        bool bActive = Section->IsActive();
                        bool bLocked = Section->IsLocked();
                        Ar << SecStart;
                        Ar << SecEnd;
                        Ar << RowIndex;
                        Ar << bActive;
                        Ar << bLocked;

                        if (UMovieSceneFloatSection* FloatSec = Cast<UMovieSceneFloatSection>(Section))
                        {
                            const FMovieSceneFloatChannel& Chan = FloatSec->GetChannel();
                            TArrayView<const FFrameNumber> Times = Chan.GetTimes();
                            TArrayView<const FMovieSceneFloatValue> Values = Chan.GetValues();
                            int32 KeyNum = Times.Num();
                            Ar << KeyNum;
                            for (int32 k = 0; k < KeyNum; ++k)
                            {
                                int32 Frame = Times[k].Value;
                                float Val = Values[k].Value;
                                uint8 Interp = (uint8)Values[k].InterpMode.GetValue();
                                uint8 TanMode = (uint8)Values[k].TangentMode.GetValue();
                                float ArrTan = Values[k].Tangent.ArriveTangent;
                                float LveTan = Values[k].Tangent.LeaveTangent;
                                float ArrTanW = Values[k].Tangent.ArriveTangentWeight;
                                float LveTanW = Values[k].Tangent.LeaveTangentWeight;
                                uint8 TanWeightMode = (uint8)Values[k].Tangent.TangentWeightMode.GetValue();
                                Ar << Frame;
                                Ar << Val;
                                Ar << Interp;
                                Ar << TanMode;
                                Ar << ArrTan;
                                Ar << LveTan;
                                Ar << ArrTanW;
                                Ar << LveTanW;
                                Ar << TanWeightMode;
                            }
                            TOptional<float> Def = Chan.GetDefault();
                            bool bHasDef = Def.IsSet();
                            float DefVal = Def.Get(0.0f);
                            Ar << bHasDef;
                            Ar << DefVal;
                        }
                        else if (UMovieSceneBoolSection* BoolSec = Cast<UMovieSceneBoolSection>(Section))
                        {
                            const FMovieSceneBoolChannel& Chan = BoolSec->GetChannel();
                            TArrayView<const FFrameNumber> Times = Chan.GetTimes();
                            TArrayView<const bool> Values = Chan.GetValues();
                            int32 KeyNum = Times.Num();
                            Ar << KeyNum;
                            for (int32 k = 0; k < KeyNum; ++k)
                            {
                                int32 Frame = Times[k].Value;
                                bool Val = Values[k];
                                Ar << Frame;
                                Ar << Val;
                            }
                            TOptional<bool> Def = Chan.GetDefault();
                            bool bHasDef = Def.IsSet();
                            bool DefVal = Def.Get(false);
                            Ar << bHasDef;
                            Ar << DefVal;
                        }
                    }
                }
            }

            // Master tracks
            TArray<UMovieSceneTrack*> MasterTracks = MS->GetTracks();
            int32 MasterTrackNum = MasterTracks.Num();
            Ar << MasterTrackNum;
            for (UMovieSceneTrack* Track : MasterTracks)
            {
                if (!Track)
                {
                    continue;
                }
                FString TrackClass = Track->GetClass()->GetPathName();
                FString TrackName = Track->GetTrackName().ToString();
                Ar << TrackClass;
                Ar << TrackName;
            }
        }
    }
}

struct FCortexUMGAnimationBindingFixture
{
    explicit FCortexUMGAnimationBindingFixture(FAutomationTestBase& Test)
        : TestRef(Test)
    {
        UPackage* TestPackage = CreatePackage(
            *(TEXT("/Temp/CortexUMGAnimationBindingFixture_") + FGuid::NewGuid().ToString()));

        UWidgetBlueprint* WBP = NewObject<UWidgetBlueprint>(
            TestPackage, TEXT("WBP_AnimBindingFixture"), RF_Public | RF_Standalone | RF_Transactional);
        WBP->ParentClass = UUserWidget::StaticClass();
        WBP->WidgetTree = NewObject<UWidgetTree>(WBP, TEXT("WidgetTree"));

        UCanvasPanel* Root = WBP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
        WBP->WidgetTree->RootWidget = Root;

        USizeBox* BodySizeBox = WBP->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("BodySizeBox"));
        Root->AddChild(BodySizeBox);

        UBorder* BorderBody = WBP->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BorderBody"));
        Root->AddChild(BorderBody);

        UImage* StorylineIcon = WBP->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("StorylineIcon"));
        Root->AddChild(StorylineIcon);

        UWidgetAnimation* Anim = NewObject<UWidgetAnimation>(WBP, TEXT("appearance"), RF_Transactional);
        UMovieScene* MS = NewObject<UMovieScene>(Anim, TEXT("appearance"));
        Anim->MovieScene = MS;

        MS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(120), FFrameNumber(720)));
        MS->SetTickResolutionDirectly(FFrameRate(24000, 1));
        MS->SetDisplayRate(FFrameRate(30000, 1001));

        FGuid Guid1 = MS->AddPossessable(TEXT("BodySizeBox"), BodySizeBox->GetClass());
        FGuid Guid2 = MS->AddPossessable(TEXT("BorderBody"), BorderBody->GetClass());
        FGuid Guid3 = MS->AddPossessable(TEXT("StorylineIcon"), StorylineIcon->GetClass());

        FWidgetAnimationBinding B1;
        B1.WidgetName = TEXT("BodySizeBox");
        B1.SlotWidgetName = NAME_None;
        B1.AnimationGuid = Guid1;
        B1.bIsRootWidget = false;
        Anim->AnimationBindings.Add(B1);

        FWidgetAnimationBinding B2;
        B2.WidgetName = TEXT("BorderBody");
        B2.SlotWidgetName = NAME_None;
        B2.AnimationGuid = Guid2;
        B2.bIsRootWidget = false;
        Anim->AnimationBindings.Add(B2);

        FWidgetAnimationBinding B3;
        B3.WidgetName = TEXT("StorylineIcon");
        B3.SlotWidgetName = NAME_None;
        B3.AnimationGuid = Guid3;
        B3.bIsRootWidget = false;
        Anim->AnimationBindings.Add(B3);

        // Track 1 on Guid1 (BodySizeBox): WidthOverride
        UMovieSceneFloatTrack* FloatTrack1 = MS->AddTrack<UMovieSceneFloatTrack>(Guid1);
        FloatTrack1->SetPropertyNameAndPath(FName("WidthOverride"), TEXT("WidthOverride"));
        UMovieSceneFloatSection* Section1 = Cast<UMovieSceneFloatSection>(FloatTrack1->CreateNewSection());
        FloatTrack1->AddSection(*Section1);
        Section1->SetRange(TRange<FFrameNumber>(FFrameNumber(120), FFrameNumber(720)));
        FMovieSceneFloatValue K1(100.0f);
        K1.InterpMode = RCIM_Cubic;
        K1.TangentMode = RCTM_User;
        K1.Tangent.ArriveTangent = 1.5f;
        K1.Tangent.LeaveTangent = 2.0f;
        FMovieSceneFloatValue K2(200.0f);
        K2.InterpMode = RCIM_Linear;
        FMovieSceneFloatValue K3(300.0f);
        K3.InterpMode = RCIM_Constant;
        Section1->GetChannel().AddKeys({ FFrameNumber(120), FFrameNumber(240), FFrameNumber(600) }, { K1, K2, K3 });
        Section1->GetChannel().SetDefault(50.0f);

        // Track 2 on Guid2 (BorderBody): HeightOverride
        UMovieSceneFloatTrack* FloatTrack2 = MS->AddTrack<UMovieSceneFloatTrack>(Guid2);
        FloatTrack2->SetPropertyNameAndPath(FName("HeightOverride"), TEXT("HeightOverride"));
        UMovieSceneFloatSection* Section2 = Cast<UMovieSceneFloatSection>(FloatTrack2->CreateNewSection());
        FloatTrack2->AddSection(*Section2);
        Section2->SetRange(TRange<FFrameNumber>(FFrameNumber(120), FFrameNumber(720)));
        Section2->GetChannel().AddKeys(
            { FFrameNumber(120), FFrameNumber(600) },
            { FMovieSceneFloatValue(150.0f), FMovieSceneFloatValue(350.0f) });
        Section2->GetChannel().SetDefault(75.0f);

        // Track 3 on Guid2 (BorderBody): RenderOpacity
        UMovieSceneFloatTrack* FloatTrack3 = MS->AddTrack<UMovieSceneFloatTrack>(Guid2);
        FloatTrack3->SetPropertyNameAndPath(FName("RenderOpacity"), TEXT("RenderOpacity"));
        UMovieSceneFloatSection* Section3 = Cast<UMovieSceneFloatSection>(FloatTrack3->CreateNewSection());
        FloatTrack3->AddSection(*Section3);
        Section3->SetRange(TRange<FFrameNumber>(FFrameNumber(120), FFrameNumber(720)));
        Section3->GetChannel().AddKeys(
            { FFrameNumber(120), FFrameNumber(240) },
            { FMovieSceneFloatValue(0.0f), FMovieSceneFloatValue(1.0f) });
        Section3->GetChannel().SetDefault(0.0f);

        // Track 4 on Guid3 (StorylineIcon): Visibility
        UMovieSceneBoolTrack* BoolTrack = MS->AddTrack<UMovieSceneBoolTrack>(Guid3);
        BoolTrack->SetPropertyNameAndPath(FName("Visibility"), TEXT("Visibility"));
        UMovieSceneBoolSection* Section4 = Cast<UMovieSceneBoolSection>(BoolTrack->CreateNewSection());
        BoolTrack->AddSection(*Section4);
        Section4->SetRange(TRange<FFrameNumber>(FFrameNumber(120), FFrameNumber(720)));
        Section4->GetChannel().AddKeys({ FFrameNumber(120), FFrameNumber(240) }, { true, false });
        Section4->GetChannel().SetDefault(true);

        // Event/master track
        UMovieSceneEventTrack* EventTrack = MS->AddTrack<UMovieSceneEventTrack>();
        UMovieSceneSection* EventSec = EventTrack->CreateNewSection();
        if (EventSec)
        {
            EventTrack->AddSection(*EventSec);
            EventSec->SetRange(TRange<FFrameNumber>(FFrameNumber(120), FFrameNumber(720)));
        }

        WBP->Animations.Add(Anim);

        // Second unrelated animation: idle
        UWidgetAnimation* IdleAnim = NewObject<UWidgetAnimation>(WBP, TEXT("idle"), RF_Transactional);
        UMovieScene* IdleMS = NewObject<UMovieScene>(IdleAnim, TEXT("idle"));
        IdleAnim->MovieScene = IdleMS;
        IdleMS->SetPlaybackRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(24000)));
        WBP->Animations.Add(IdleAnim);

        Blueprint = TStrongObjectPtr<UWidgetBlueprint>(WBP);

        Router.RegisterDomain(TEXT("umg"), TEXT("Cortex UMG"), TEXT("1.0.1"),
            MakeShared<FCortexUMGCommandHandler>());
    }

    ~FCortexUMGAnimationBindingFixture()
    {
        if (Blueprint.IsValid())
        {
            UWidgetBlueprint* WBP = Blueprint.Get();
            UPackage* Pkg = WBP->GetPackage();
            WBP->MarkAsGarbage();
            if (Pkg)
            {
                Pkg->ClearDirtyFlag();
            }
            Blueprint.Reset();
        }
    }

    TStrongObjectPtr<UWidgetBlueprint> Blueprint;
    FCortexCommandRouter Router;
    FAutomationTestBase& TestRef;

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

    TArray<uint8> CaptureAllAuthoredState() const
    {
        TArray<uint8> Buffer;
        FMemoryWriter Ar(Buffer);
        if (Blueprint.IsValid())
        {
            for (UWidgetAnimation* Anim : Blueprint->Animations)
            {
                CortexUMGAnimationBindingTestUtils::SerializeAnimationAuthoredData(Anim, Ar, nullptr);
            }
        }
        return Buffer;
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

    void ChangeRetainedFloatKey(float NewValue)
    {
        if (!Blueprint.IsValid())
        {
            return;
        }
        for (UWidgetAnimation* Anim : Blueprint->Animations)
        {
            if (Anim && Anim->GetName() == TEXT("appearance") && Anim->MovieScene)
            {
            const UMovieScene* ConstMS = Anim->MovieScene;
            for (const FMovieSceneBinding& Binding : ConstMS->GetBindings())
                {
                    for (UMovieSceneTrack* Track : Binding.GetTracks())
                    {
                        if (UMovieSceneFloatTrack* FloatTrack = Cast<UMovieSceneFloatTrack>(Track))
                        {
                            for (UMovieSceneSection* Section : FloatTrack->GetAllSections())
                            {
                                if (UMovieSceneFloatSection* FloatSec = Cast<UMovieSceneFloatSection>(Section))
                                {
                                    FMovieSceneFloatChannel& Chan = FloatSec->GetChannel();
                                    TArrayView<const FFrameNumber> TimesView = Chan.GetTimes();
                                    TArrayView<const FMovieSceneFloatValue> ValuesView = Chan.GetValues();
                                    if (ValuesView.Num() > 0)
                                    {
                                        TArray<FFrameNumber> Times;
                                        Times.Append(TimesView.GetData(), TimesView.Num());
                                        TArray<FMovieSceneFloatValue> Values;
                                        Values.Append(ValuesView.GetData(), ValuesView.Num());
                                        Values[0].Value = NewValue;
                                        Chan.Set(Times, Values);
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
};
