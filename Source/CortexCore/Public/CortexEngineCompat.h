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

	// Registers a string table asset under its own table ID. UE 5.4 UStringTable constructors
	// already register every non-CDO table, /Temp/ ones included; newer engines skip temporary
	// packages and leave that to the caller. Registering the same table again on 5.4 trips the
	// registry's duplicate-ID check, so skip only when the registry already holds this very table.
	inline void RegisterStringTableAsset(UStringTable* Table)
	{
		if (!Table)
		{
			return;
		}
		const FName TableId = Table->GetStringTableId();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
		const FStringTableConstPtr Existing = FStringTableRegistry::Get().FindStringTable(TableId);
		if (Existing.IsValid() && Existing->GetOwnerAsset() == Table)
		{
			return;
		}
#endif
		FStringTableRegistry::Get().RegisterStringTable(TableId, Table->GetMutableStringTable());
	}

	inline void ForEachObjectWithPackage(
		const UPackage* Outer,
		TFunctionRef<bool(UObject*)> Operation,
		bool bIncludeNestedObjects = true)
	{
		CortexForEachObjectWithPackage(Outer, Operation, bIncludeNestedObjects);
	}
}
