#pragma once

#include "CortexVersionCompat.h"
#include "Dom/JsonObject.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/StringTableRegistry.h"
#include "Misc/EngineVersionComparison.h"
#include "Templates/Function.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"

namespace CortexEngineCompat
{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	inline FString JsonKeyToString(const FString& Key)
	{
		return CortexJsonKeyToString(Key);
	}
#else
	inline FString JsonKeyToString(const FJsonObject::FStringType& Key)
	{
		return CortexJsonKeyToString(Key);
	}
#endif

	inline void SetStringTableSourceString(
		FStringTable& StringTable,
		const FTextKey& Key,
		const FString& SourceString)
	{
		CortexSetSourceString(StringTable, Key, SourceString);
#if !UE_VERSION_OLDER_THAN(5, 8, 0)
		if (UStringTable* OwnerAsset = StringTable.GetOwnerAsset())
		{
			const FName TableId = OwnerAsset->GetStringTableId();
			if (!FStringTableRegistry::Get().FindStringTable(TableId).IsValid())
			{
				FStringTableRegistry::Get().RegisterStringTable(TableId, StringTable.AsShared());
			}
		}
#endif
	}

	inline void ForEachObjectWithPackage(
		const UPackage* Outer,
		TFunctionRef<bool(UObject*)> Operation,
		bool bIncludeNestedObjects = true)
	{
		CortexForEachObjectWithPackage(Outer, Operation, bIncludeNestedObjects);
	}
}
