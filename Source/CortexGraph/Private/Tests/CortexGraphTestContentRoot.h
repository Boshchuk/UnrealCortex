#pragma once

#include "CoreMinimal.h"
#include "CortexEditorUtils.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#if WITH_EDITOR && WITH_AUTOMATION_TESTS

/**
 * Test-only authorization for the transient `/Temp` mount.
 *
 * The patch route applies the same shared writable-content-root policy as every other graph
 * mutator (`FCortexEditorUtils::IsWritableMountedContentRoots`), and `/Temp` is not a project
 * content root. The patch fixtures are intentionally transient in-memory packages under `/Temp`,
 * so they register that one root through the sanctioned test hook instead of the production policy
 * growing a test-only exception or a second allowlist.
 *
 * Registration is idempotent (`AddTestWritableContentRoot` de-duplicates) and lives for the
 * automation process only: it is called from test request builders, which never run outside a test.
 */
inline void EnsureCortexGraphTestTempContentRoot()
{
	static const bool bRegistered = []()
	{
		FCortexEditorUtils::AddTestWritableContentRoot(TEXT("/Temp"));
		return true;
	}();
	(void)bRegistered;
}

/** Preserve the asset's leaf name while keeping each fixture's disk package isolated. */
inline UPackage* CreateCortexGraphTestPackage(const TCHAR* AssetName, FString& OutOwnedPackageName)
{
	EnsureCortexGraphTestTempContentRoot();
	OutOwnedPackageName = FString::Printf(TEXT("/Game/Temp/CortexGraphFixture_%s/%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits), AssetName);
	return CreatePackage(*OutOwnedPackageName);
}

/** Delete only this fixture's file and empty GUID directory, including after an explicit unload. */
inline void DeleteCortexGraphTestPackageFile(UPackage* Package, FString& OwnedPackageName)
{
	if (OwnedPackageName.IsEmpty())
	{
		return;
	}

	const FString Filename = FPackageName::LongPackageNameToFilename(
		OwnedPackageName, FPackageName::GetAssetPackageExtension());
	const FString Directory = FPaths::GetPath(Filename);
	IFileManager& FileManager = IFileManager::Get();
	if (FileManager.FileExists(*Filename))
	{
		UPackage* ResidentPackage = Package ? Package : FindPackage(nullptr, *OwnedPackageName);
		if (ResidentPackage)
		{
			ResetLoaders(ResidentPackage);
		}
		if (!FileManager.Delete(*Filename))
		{
			if (FAutomationTestBase* Test = FAutomationTestFramework::Get().GetCurrentTest())
			{
				Test->AddError(FString::Printf(TEXT("Failed to delete owned Graph fixture file: %s"), *Filename));
			}
			return;
		}
	}
	if (FileManager.DirectoryExists(*Directory) && !FileManager.DeleteDirectory(*Directory, false, false))
	{
		if (FAutomationTestBase* Test = FAutomationTestFramework::Get().GetCurrentTest())
		{
			Test->AddError(FString::Printf(TEXT("Failed to delete empty owned Graph fixture directory: %s"), *Directory));
		}
		return;
	}
	OwnedPackageName.Reset();
}

#endif // WITH_EDITOR && WITH_AUTOMATION_TESTS
