#pragma once

#include "CoreMinimal.h"
#include "CortexAssetFingerprint.h"
#include "CortexCommandRouter.h"
#include "WidgetBlueprint.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "Dom/JsonObject.h"

struct FCortexAnimationBindingPreflight
{
    UWidgetBlueprint* Blueprint = nullptr;
    UWidgetAnimation* Animation = nullptr;
    UMovieScene* MovieScene = nullptr;

    int32 MatchedRecordIndex = INDEX_NONE;
    FWidgetAnimationBinding MatchedBinding;
    int32 GuidSharingCount = 0;

    bool bTargetExists = false;
    bool bSlotExists = false;
    bool bPossessableExists = false;

    FCortexAssetFingerprint CurrentFingerprint;

    TArray<FWidgetAnimationBinding> ProjectedRemainingBindings;
};

struct FCortexUMGAnimationBindingFingerprint
{
    FCortexAssetFingerprint Base;
    FString AssetPath;
    FString AnimationName;
    FString Digest;

    TSharedPtr<FJsonObject> ToJson() const;
};

namespace CortexUMGAnimationBindingUtils
{
    /** Computes the canonical SHA256 digest of the authored animation state */
    FString ComputeAnimationDigest(
        const FString& AssetPath,
        const FString& AnimName,
        UWidgetAnimation* Animation);

    /** Computes the full fingerprint including base package and domain signature */
    FCortexUMGAnimationBindingFingerprint ComputeFingerprint(
        UWidgetBlueprint* Blueprint,
        UWidgetAnimation* Animation);

    /** Verifies that the expected fingerprint matches the live state */
    bool VerifyFingerprint(
        const TSharedPtr<FJsonObject>& ExpectedFingerprint,
        const FCortexUMGAnimationBindingFingerprint& LiveFingerprint,
        const FString& ExpectedAssetPath,
        const FString& ExpectedAnimName,
        FString& OutError);
}
