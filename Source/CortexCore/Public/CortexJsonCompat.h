// Copyright Eugene Telyatnik. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Misc/EngineVersionComparison.h"

/**
 * Engine-version seam for FJsonObject's key storage.
 *
 * UE 5.8 changed FJsonObject::Values from TMap<FString, TSharedPtr<FJsonValue>> to
 * TMap<UE::FSharedString, TSharedPtr<FJsonValue>> so that recurring JSON keys share one
 * allocation, and reworked the public interface onto FStringView. That breaks every
 * `for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)` (the explicit
 * TPair binds to a temporary, which -Wrange-loop-construct rejects as an error) and every
 * Values.Find/GetKeys call that passes an FString.
 *
 * Epic offers UE_JSONOBJECT_LEGACY_STRING_KEYS=1 as a temporary escape hatch, but that switch
 * changes FJsonObject's layout: defining it for this plugin alone, while the engine's prebuilt
 * Json module uses the default, would be an ABI mismatch. It is also slated for removal. So we
 * port to the new interface and keep FString-shaped call sites behind these helpers instead.
 *
 * Use `const auto&` for the loop variable when iterating Values, then route key uses through
 * KeyToString/MakeKey. Everything here compiles unchanged on UE 5.4 through 5.8.
 */
namespace CortexJson
{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
	/** Key type FJsonObject::Values uses on this engine version. */
	using FFieldKey = FString;
#else
	using FFieldKey = FJsonObject::FStringType;
#endif

	/** One FJsonObject::Values entry on this engine version. */
	using FFieldPair = TPair<FFieldKey, TSharedPtr<FJsonValue>>;

	/** Converts a JSON field key to FString, whatever the engine stores keys as. */
	[[nodiscard]] inline FString KeyToString(const FFieldKey& Key)
	{
		return FString(Key);
	}

	/** Builds a key in the engine's native representation from an FString-shaped name. */
	[[nodiscard]] inline FFieldKey MakeKey(FStringView Name)
	{
		return FFieldKey(Name);
	}

	/**
	 * Looks up a field by name without the caller depending on the engine's key type.
	 * Returns nullptr when the object is invalid or the field is absent.
	 */
	[[nodiscard]] inline const TSharedPtr<FJsonValue>* FindField(const TSharedPtr<FJsonObject>& Object, FStringView Name)
	{
		return Object.IsValid() ? Object->Values.Find(MakeKey(Name)) : nullptr;
	}

	/** As FindField, for a TSharedRef that is always valid. */
	[[nodiscard]] inline const TSharedPtr<FJsonValue>* FindField(const TSharedRef<FJsonObject>& Object, FStringView Name)
	{
		return Object->Values.Find(MakeKey(Name));
	}

	/**
	 * Replaces OutNames with the object's field names as FStrings.
	 * Matches TMap::GetKeys(TArray&), which resets its output before filling.
	 */
	inline void GetFieldNames(const TSharedPtr<FJsonObject>& Object, TArray<FString>& OutNames)
	{
		OutNames.Reset();
		if (!Object.IsValid())
		{
			return;
		}

		OutNames.Reserve(Object->Values.Num());
		for (const auto& Pair : Object->Values)
		{
			OutNames.Add(KeyToString(Pair.Key));
		}
	}

	/**
	 * Adds the object's field names to OutNames without clearing it, so several objects can be
	 * unioned into one set.
	 *
	 * Deliberately NOT named GetFieldNames: TMap::GetKeys(TSet&) resets its output too, so code
	 * that called GetKeys twice into one TSet expecting a union actually kept only the last
	 * object's keys. Use this when a union is intended.
	 */
	inline void AppendFieldNames(const TSharedPtr<FJsonObject>& Object, TSet<FString>& OutNames)
	{
		if (!Object.IsValid())
		{
			return;
		}

		OutNames.Reserve(OutNames.Num() + Object->Values.Num());
		for (const auto& Pair : Object->Values)
		{
			OutNames.Add(KeyToString(Pair.Key));
		}
	}
}
