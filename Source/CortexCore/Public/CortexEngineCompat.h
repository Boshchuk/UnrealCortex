#pragma once

#include "Dom/JsonObject.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/EngineVersionComparison.h"

namespace CortexEngineCompat
{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	inline FString JsonKeyToString(const FString& Key)
	{
		return Key;
	}
#else
	inline FString JsonKeyToString(const FJsonObject::FStringType& Key)
	{
		return FString(Key.ToView());
	}
#endif

	inline void SetStringTableSourceString(
		FStringTable& StringTable,
		const FTextKey& Key,
		const FString& SourceString)
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		StringTable.SetSourceString(Key, SourceString);
#else
		StringTable.SetSourceString(Key, SourceString, FString());
#endif
	}
}
