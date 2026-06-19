#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectGlobals.h"

// Shared helpers for CortexAnimation ops. Inline + namespaced so they survive
// unity builds (anonymous-namespace copies in multiple .cpp collide when batched).
namespace CortexAnim
{
	/** Load an asset of type T by object path, or null. */
	template <typename T>
	T* LoadTyped(const FString& Path)
	{
		return LoadObject<T>(nullptr, *Path);
	}
}
