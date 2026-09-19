#include "Operations/CortexUMGAnimationBindingUtils.h"
#include "Serialization/MemoryWriter.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneBoolSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "String/BytesToHex.h"

namespace
{
    // Standard SHA-256 implementation
    struct FSHA256State
    {
        uint32 State[8];
        uint64 BitCount;
        uint8 Buffer[64];
    };

    inline uint32 RotR(uint32 Value, uint32 Shift)
    {
        return (Value >> Shift) | (Value << (32 - Shift));
    }

    void SHA256Init(FSHA256State& Ctx)
    {
        Ctx.State[0] = 0x6a09e667;
        Ctx.State[1] = 0xbb67ae85;
        Ctx.State[2] = 0x3c6ef372;
        Ctx.State[3] = 0xa54ff53a;
        Ctx.State[4] = 0x510e527f;
        Ctx.State[5] = 0x9b05688c;
        Ctx.State[6] = 0x1f83d9ab;
        Ctx.State[7] = 0x5be0cd19;
        Ctx.BitCount = 0;
    }

    void SHA256Transform(FSHA256State& Ctx, const uint8 Data[64])
    {
        static const uint32 K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
        };

        uint32 W[64];
        for (int32 i = 0; i < 16; ++i)
        {
            W[i] = (static_cast<uint32>(Data[i * 4]) << 24)
                 | (static_cast<uint32>(Data[i * 4 + 1]) << 16)
                 | (static_cast<uint32>(Data[i * 4 + 2]) << 8)
                 | (static_cast<uint32>(Data[i * 4 + 3]));
        }
        for (int32 i = 16; i < 64; ++i)
        {
            const uint32 S0 = RotR(W[i - 15], 7) ^ RotR(W[i - 15], 18) ^ (W[i - 15] >> 3);
            const uint32 S1 = RotR(W[i - 2], 17) ^ RotR(W[i - 2], 19) ^ (W[i - 2] >> 10);
            W[i] = W[i - 16] + S0 + W[i - 7] + S1;
        }

        uint32 A = Ctx.State[0];
        uint32 B = Ctx.State[1];
        uint32 C = Ctx.State[2];
        uint32 D = Ctx.State[3];
        uint32 E = Ctx.State[4];
        uint32 F = Ctx.State[5];
        uint32 G = Ctx.State[6];
        uint32 H = Ctx.State[7];

        for (int32 i = 0; i < 64; ++i)
        {
            const uint32 S1 = RotR(E, 6) ^ RotR(E, 11) ^ RotR(E, 25);
            const uint32 Ch = (E & F) ^ ((~E) & G);
            const uint32 Temp1 = H + S1 + Ch + K[i] + W[i];
            const uint32 S0 = RotR(A, 2) ^ RotR(A, 13) ^ RotR(A, 22);
            const uint32 Maj = (A & B) ^ (A & C) ^ (B & C);
            const uint32 Temp2 = S0 + Maj;

            H = G;
            G = F;
            F = E;
            E = D + Temp1;
            D = C;
            C = B;
            B = A;
            A = Temp1 + Temp2;
        }

        Ctx.State[0] += A;
        Ctx.State[1] += B;
        Ctx.State[2] += C;
        Ctx.State[3] += D;
        Ctx.State[4] += E;
        Ctx.State[5] += F;
        Ctx.State[6] += G;
        Ctx.State[7] += H;
    }

    void SHA256Update(FSHA256State& Ctx, const uint8* Data, uint64 Length)
    {
        uint32 BufferIndex = static_cast<uint32>((Ctx.BitCount / 8) % 64);
        Ctx.BitCount += Length * 8;

        for (uint64 i = 0; i < Length; ++i)
        {
            Ctx.Buffer[BufferIndex++] = Data[i];
            if (BufferIndex == 64)
            {
                SHA256Transform(Ctx, Ctx.Buffer);
                BufferIndex = 0;
            }
        }
    }

    void SHA256Final(FSHA256State& Ctx, uint8 OutDigest[32])
    {
        uint32 BufferIndex = static_cast<uint32>((Ctx.BitCount / 8) % 64);
        Ctx.Buffer[BufferIndex++] = 0x80;

        if (BufferIndex > 56)
        {
            while (BufferIndex < 64)
            {
                Ctx.Buffer[BufferIndex++] = 0x00;
            }
            SHA256Transform(Ctx, Ctx.Buffer);
            BufferIndex = 0;
        }

        while (BufferIndex < 56)
        {
            Ctx.Buffer[BufferIndex++] = 0x00;
        }

        for (int32 i = 0; i < 8; ++i)
        {
            Ctx.Buffer[56 + i] = static_cast<uint8>((Ctx.BitCount >> ((7 - i) * 8)) & 0xFF);
        }
        SHA256Transform(Ctx, Ctx.Buffer);

        for (int32 i = 0; i < 8; ++i)
        {
            OutDigest[i * 4] = static_cast<uint8>((Ctx.State[i] >> 24) & 0xFF);
            OutDigest[i * 4 + 1] = static_cast<uint8>((Ctx.State[i] >> 16) & 0xFF);
            OutDigest[i * 4 + 2] = static_cast<uint8>((Ctx.State[i] >> 8) & 0xFF);
            OutDigest[i * 4 + 3] = static_cast<uint8>(Ctx.State[i] & 0xFF);
        }
    }

    FString ComputeSHA256Hex(const uint8* Data, uint64 Length)
    {
        FSHA256State Ctx;
        SHA256Init(Ctx);
        SHA256Update(Ctx, Data, Length);
        uint8 Digest[32];
        SHA256Final(Ctx, Digest);
        return BytesToHex(Digest, sizeof(Digest)).ToLower();
    }
}

TSharedPtr<FJsonObject> FCortexUMGAnimationBindingFingerprint::ToJson() const
{
    TSharedPtr<FJsonObject> Json = Base.ToJson();
    TSharedPtr<FJsonObject> DomainSig = MakeShared<FJsonObject>();
    DomainSig->SetNumberField(TEXT("version"), 1);
    DomainSig->SetStringField(TEXT("scope"), TEXT("umg.animation_binding"));
    DomainSig->SetStringField(TEXT("asset_path"), AssetPath);
    DomainSig->SetStringField(TEXT("animation_name"), AnimationName);
    DomainSig->SetStringField(TEXT("digest"), Digest);
    Json->SetObjectField(TEXT("domain_signature"), DomainSig);
    return Json;
}

namespace CortexUMGAnimationBindingUtils
{
    FString ComputeAnimationDigest(
        const FString& AssetPath,
        const FString& AnimName,
        UWidgetAnimation* Animation)
    {
        TArray<uint8> Buffer;
        FMemoryWriter Ar(Buffer);

        FString AP = AssetPath;
        FString AN = AnimName;
        Ar << AP;
        Ar << AN;

        if (Animation)
        {
            TArray<FWidgetAnimationBinding> SortedBindings = Animation->AnimationBindings;
            SortedBindings.Sort([](const FWidgetAnimationBinding& A, const FWidgetAnimationBinding& B)
            {
                return A.AnimationGuid < B.AnimationGuid;
            });

            int32 BindingNum = SortedBindings.Num();
            Ar << BindingNum;
            for (const FWidgetAnimationBinding& Binding : SortedBindings)
            {
                FString WName = Binding.WidgetName.ToString();
                FString SName = Binding.SlotWidgetName.ToString();
                FGuid AGuid = Binding.AnimationGuid;
                bool bRoot = Binding.bIsRootWidget;
                FString DynFunc = Binding.DynamicBinding.Function ? Binding.DynamicBinding.Function->GetPathName() : FString();
                Ar << WName;
                Ar << SName;
                Ar << AGuid;
                Ar << bRoot;
                Ar << DynFunc;
            }

            if (Animation->MovieScene)
            {
                UMovieScene* MS = Animation->MovieScene;

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

                const UMovieScene* ConstMS = MS;
                TArray<FMovieSceneBinding> SortedMSBindings = ConstMS->GetBindings();
                SortedMSBindings.Sort([](const FMovieSceneBinding& A, const FMovieSceneBinding& B)
                {
                    return A.GetObjectGuid() < B.GetObjectGuid();
                });

                int32 MSBindingNum = SortedMSBindings.Num();
                Ar << MSBindingNum;
                for (const FMovieSceneBinding& MSB : SortedMSBindings)
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
                        Sections.Sort([](const UMovieSceneSection& A, const UMovieSceneSection& B)
                        {
                            int32 StartA = A.GetRange().GetLowerBound().IsOpen() ? 0 : A.GetRange().GetLowerBoundValue().Value;
                            int32 StartB = B.GetRange().GetLowerBound().IsOpen() ? 0 : B.GetRange().GetLowerBoundValue().Value;
                            return StartA < StartB;
                        });

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

                TArray<UMovieSceneTrack*> MasterTracks = ConstMS->GetTracks();
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

        return ComputeSHA256Hex(Buffer.GetData(), Buffer.Num());
    }

    FCortexUMGAnimationBindingFingerprint ComputeFingerprint(
        UWidgetBlueprint* Blueprint,
        UWidgetAnimation* Animation)
    {
        FCortexUMGAnimationBindingFingerprint Fingerprint;
        Fingerprint.Base = MakeObjectAssetFingerprint(Blueprint);
        Fingerprint.AssetPath = Blueprint ? Blueprint->GetPathName() : FString();
        Fingerprint.AnimationName = Animation ? Animation->GetName() : FString();
        Fingerprint.Digest = ComputeAnimationDigest(Fingerprint.AssetPath, Fingerprint.AnimationName, Animation);
        return Fingerprint;
    }

    bool VerifyFingerprint(
        const TSharedPtr<FJsonObject>& ExpectedFingerprint,
        const FCortexUMGAnimationBindingFingerprint& LiveFingerprint,
        const FString& ExpectedAssetPath,
        const FString& ExpectedAnimName,
        FString& OutError)
    {
        if (!ExpectedFingerprint.IsValid())
        {
            OutError = TEXT("Expected fingerprint is null or invalid");
            return false;
        }

        if (ExpectedFingerprint->HasField(TEXT("package_saved_hash")))
        {
            FString ExpectedSavedHash = ExpectedFingerprint->GetStringField(TEXT("package_saved_hash"));
            if (ExpectedSavedHash != LiveFingerprint.Base.PackageSavedHash)
            {
                OutError = FString::Printf(TEXT("package_saved_hash mismatch: expected '%s', live '%s'"),
                    *ExpectedSavedHash, *LiveFingerprint.Base.PackageSavedHash);
                return false;
            }
        }

        if (ExpectedFingerprint->HasField(TEXT("is_dirty")))
        {
            bool bExpectedDirty = ExpectedFingerprint->GetBoolField(TEXT("is_dirty"));
            if (bExpectedDirty != LiveFingerprint.Base.bIsDirty)
            {
                OutError = FString::Printf(TEXT("is_dirty mismatch: expected %d, live %d"),
                    bExpectedDirty ? 1 : 0, LiveFingerprint.Base.bIsDirty ? 1 : 0);
                return false;
            }
        }

        if (ExpectedFingerprint->HasField(TEXT("dirty_epoch")))
        {
            FString ExpectedDirtyEpoch = ExpectedFingerprint->GetStringField(TEXT("dirty_epoch"));
            FString LiveDirtyEpoch = FString::Printf(TEXT("%llu"), LiveFingerprint.Base.DirtyEpoch);
            if (ExpectedDirtyEpoch != LiveDirtyEpoch)
            {
                OutError = FString::Printf(TEXT("dirty_epoch mismatch: expected '%s', live '%s'"),
                    *ExpectedDirtyEpoch, *LiveDirtyEpoch);
                return false;
            }
        }

        const TSharedPtr<FJsonObject>* DomainSig = nullptr;
        if (!ExpectedFingerprint->TryGetObjectField(TEXT("domain_signature"), DomainSig) || !DomainSig || !DomainSig->IsValid())
        {
            OutError = TEXT("Expected fingerprint is missing required 'domain_signature' object");
            return false;
        }

        int32 Version = (*DomainSig)->GetIntegerField(TEXT("version"));
        if (Version != 1)
        {
            OutError = FString::Printf(TEXT("Unsupported domain_signature version: %d (expected 1)"), Version);
            return false;
        }

        FString Scope = (*DomainSig)->GetStringField(TEXT("scope"));
        if (Scope != TEXT("umg.animation_binding"))
        {
            OutError = FString::Printf(TEXT("Invalid domain_signature scope: '%s' (expected 'umg.animation_binding')"), *Scope);
            return false;
        }

        FString AssetPath = (*DomainSig)->GetStringField(TEXT("asset_path"));
        if (AssetPath != ExpectedAssetPath)
        {
            OutError = FString::Printf(TEXT("domain_signature asset_path mismatch: expected '%s', got '%s'"),
                *ExpectedAssetPath, *AssetPath);
            return false;
        }

        FString AnimName = (*DomainSig)->GetStringField(TEXT("animation_name"));
        if (AnimName != ExpectedAnimName)
        {
            OutError = FString::Printf(TEXT("domain_signature animation_name mismatch: expected '%s', got '%s'"),
                *ExpectedAnimName, *AnimName);
            return false;
        }

        FString Digest = (*DomainSig)->GetStringField(TEXT("digest"));
        if (Digest != LiveFingerprint.Digest)
        {
            OutError = FString::Printf(TEXT("Animation content has changed: expected digest '%s', live digest '%s'"),
                *Digest, *LiveFingerprint.Digest);
            return false;
        }

        return true;
    }
}
