#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/Guid.h"
#include "ObjectTools.h"

namespace CortexStateTreeTest
{
inline FString MakeSuffix()
{
	return FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
}

inline FString MakeAssetPath(const FString& Prefix)
{
	return FString::Printf(TEXT("/Game/Temp/%s_%s"), *Prefix, *MakeSuffix());
}

inline TSharedPtr<FJsonObject> Params()
{
	return MakeShared<FJsonObject>();
}

inline FString GetTestSchemaClassPath()
{
	return TEXT("/Script/CortexStateTree.CortexStateTreeTestSchema");
}

inline void DeleteIfLoaded(const FString& AssetPath)
{
	if (UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath))
	{
		TArray<UObject*> Objects;
		Objects.Add(Asset);
		ObjectTools::ForceDeleteObjects(Objects, false);
	}
}
}
