#include "Misc/AutomationTest.h"
#include "Operations/CortexDataAssetOps.h"
#include "CortexCommandRouter.h"
#include "CortexDataCommandHandler.h"
#include "Dom/JsonObject.h"
#include "Engine/DataAsset.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "ObjectTools.h"
#include "HAL/PlatformFileManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/TextBuffer.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "Tests/CortexTestDataAsset.h"

namespace
{
	/** Clean up a test DataAsset from memory and disk. */
	void CleanupTestAsset(const FString& PackagePath)
	{
		// Guard LoadObject to prevent SkipPackage warnings.
		const FString PkgName = FPackageName::ObjectPathToPackageName(PackagePath);
		const FString ObjName = FPackageName::GetShortName(PkgName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PkgName, *ObjName);

		const bool bPackageLoaded = FindPackage(nullptr, *PkgName) != nullptr;
		const bool bPackageOnDisk = FPackageName::DoesPackageExist(PkgName);
		if (!bPackageLoaded && !bPackageOnDisk)
		{
			return;
		}

		const FString Filename = FPackageName::LongPackageNameToFilename(
			PkgName,
			FPackageName::GetAssetPackageExtension());

		UDataAsset* Asset = FindObject<UDataAsset>(nullptr, *ObjectPath);
		if (Asset == nullptr && bPackageOnDisk)
		{
			Asset = LoadObject<UDataAsset>(nullptr, *ObjectPath);
		}

		// Delete file first to avoid asset registry file watcher races.
		if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename);
		}

		if (Asset != nullptr)
		{
			UTextBuffer* DeleteGuard = nullptr;
			if (UPackage* Package = Asset->GetOutermost())
			{
				DeleteGuard = NewObject<UTextBuffer>(Package, TEXT("__CortexDeleteGuard__"), RF_Public);
			}

			TArray<UObject*> ToDelete;
			ToDelete.Add(Asset);
			ObjectTools::ForceDeleteObjects(ToDelete, false);

			if (DeleteGuard != nullptr)
			{
				DeleteGuard->ClearFlags(RF_Public | RF_Standalone);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexCreateDataAssetBasicTest,
	"Cortex.Data.DataAsset.CreateBasic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexCreateDataAssetBasicTest::RunTest(const FString& Parameters)
{
	const FString TestPath = TEXT("/Game/Temp/CortexTest_CreateDA_Basic");

	CleanupTestAsset(TestPath);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("class_name"), TEXT("CortexTestDataAsset"));
	Params->SetStringField(TEXT("asset_path"), TestPath);

	FCortexCommandResult Result = FCortexDataAssetOps::CreateDataAsset(Params);

	TestTrue(TEXT("Create should succeed"), Result.bSuccess);

	if (Result.bSuccess && Result.Data.IsValid())
	{
		FString ReturnedPath;
		Result.Data->TryGetStringField(TEXT("asset_path"), ReturnedPath);
		TestFalse(TEXT("Returned path should not be empty"), ReturnedPath.IsEmpty());

		FString ReturnedClass;
		Result.Data->TryGetStringField(TEXT("asset_class"), ReturnedClass);
		TestEqual(TEXT("Class should be CortexTestDataAsset"), ReturnedClass, TEXT("CortexTestDataAsset"));

		bool bCreated = false;
		Result.Data->TryGetBoolField(TEXT("created"), bCreated);
		TestTrue(TEXT("created flag should be true"), bCreated);
	}

	// Verify the asset can be loaded.
	UDataAsset* LoadedAsset = LoadObject<UDataAsset>(nullptr, *TestPath);
	TestNotNull(TEXT("Asset should be loadable after create"), LoadedAsset);

	// Verify on-disk file exists.
	const FString Filename = FPackageName::LongPackageNameToFilename(
		TestPath,
		FPackageName::GetAssetPackageExtension());
	TestTrue(
		TEXT("File should exist on disk"),
		FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename));

	CleanupTestAsset(TestPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexCreateDataAssetInvalidClassTest,
	"Cortex.Data.DataAsset.CreateInvalidClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexCreateDataAssetInvalidClassTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("class_name"), TEXT("NonExistentDataAssetClass_XYZ"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/Temp/CortexTest_CreateDA_Invalid"));

	FCortexCommandResult Result = FCortexDataAssetOps::CreateDataAsset(Params);

	TestFalse(TEXT("Create with invalid class should fail"), Result.bSuccess);
	TestEqual(TEXT("Error code should be CLASS_NOT_FOUND"), Result.ErrorCode, TEXT("CLASS_NOT_FOUND"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexCreateDataAssetAbstractClassTest,
	"Cortex.Data.DataAsset.CreateAbstractClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexCreateDataAssetAbstractClassTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("class_name"), TEXT("DataAsset"));
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/Temp/CortexTest_CreateDA_Abstract"));

	FCortexCommandResult Result = FCortexDataAssetOps::CreateDataAsset(Params);

	TestFalse(TEXT("Create with abstract class should fail"), Result.bSuccess);
	TestEqual(TEXT("Error code should be INVALID_OPERATION"), Result.ErrorCode, TEXT("INVALID_OPERATION"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexCreateDataAssetDuplicateTest,
	"Cortex.Data.DataAsset.CreateDuplicatePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexCreateDataAssetDuplicateTest::RunTest(const FString& Parameters)
{
	const FString TestPath = TEXT("/Game/Temp/CortexTest_CreateDA_Dup");

	CleanupTestAsset(TestPath);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("class_name"), TEXT("CortexTestDataAsset"));
	Params->SetStringField(TEXT("asset_path"), TestPath);

	FCortexCommandResult Result1 = FCortexDataAssetOps::CreateDataAsset(Params);
	TestTrue(TEXT("First create should succeed"), Result1.bSuccess);

	FCortexCommandResult Result2 = FCortexDataAssetOps::CreateDataAsset(Params);
	TestFalse(TEXT("Duplicate create should fail"), Result2.bSuccess);
	TestEqual(TEXT("Error code should be ASSET_ALREADY_EXISTS"), Result2.ErrorCode, TEXT("ASSET_ALREADY_EXISTS"));

	CleanupTestAsset(TestPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexCreateDataAssetWithPropertiesTest,
	"Cortex.Data.DataAsset.CreateWithProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexCreateDataAssetWithPropertiesTest::RunTest(const FString& Parameters)
{
	const FString TestPath = TEXT("/Game/Temp/CortexTest_CreateDA_Props");

	CleanupTestAsset(TestPath);

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
	Props->SetStringField(TEXT("TestProperty"), TEXT("Hello from CRUD"));
	Props->SetNumberField(TEXT("TestNumber"), 42);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("class_name"), TEXT("CortexTestDataAsset"));
	Params->SetStringField(TEXT("asset_path"), TestPath);
	Params->SetObjectField(TEXT("properties"), Props);

	FCortexCommandResult Result = FCortexDataAssetOps::CreateDataAsset(Params);

	TestTrue(TEXT("Create with properties should succeed"), Result.bSuccess);

	// Load and verify properties were applied.
	const FString ObjPath = FString::Printf(TEXT("%s.%s"),
		*TestPath, *FPackageName::GetShortName(TestPath));
	UCortexTestDataAsset* Asset = LoadObject<UCortexTestDataAsset>(nullptr, *ObjPath);
	TestNotNull(TEXT("Asset should be loadable"), Asset);

	if (Asset != nullptr)
	{
		TestEqual(TEXT("TestProperty should be set"), Asset->TestProperty, TEXT("Hello from CRUD"));
		TestEqual(TEXT("TestNumber should be set"), Asset->TestNumber, 42);
	}

	CleanupTestAsset(TestPath);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDeleteDataAssetBasicTest,
	"Cortex.Data.DataAsset.DeleteBasic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDeleteDataAssetBasicTest::RunTest(const FString& Parameters)
{
	const FString TestPath = TEXT("/Game/Temp/CortexTest_DeleteDA");

	CleanupTestAsset(TestPath);

	// Create a DataAsset manually to delete.
	const FString AssetName = FPackageName::GetShortName(TestPath);
	UPackage* TestPkg = CreatePackage(*TestPath);
	UDataAsset* TestAsset = NewObject<UDataAsset>(
		TestPkg,
		UCortexTestDataAsset::StaticClass(),
		FName(*AssetName),
		RF_Public | RF_Standalone);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		TestPkg->GetName(),
		FPackageName::GetAssetPackageExtension());
	const bool bSaved = UPackage::SavePackage(TestPkg, TestAsset, *Filename, SaveArgs);
	TestTrue(TEXT("Setup: asset should save to disk"), bSaved);

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("asset_path"), TestPath);

	FCortexCommandResult Result = FCortexDataAssetOps::DeleteDataAsset(Params);
	TestTrue(TEXT("Delete should succeed"), Result.bSuccess);

	if (Result.bSuccess && Result.Data.IsValid())
	{
		bool bDeleted = false;
		Result.Data->TryGetBoolField(TEXT("deleted"), bDeleted);
		TestTrue(TEXT("deleted flag should be true"), bDeleted);
	}

	TestFalse(
		TEXT("File should be deleted from disk"),
		FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDeleteDataAssetNotFoundTest,
	"Cortex.Data.DataAsset.DeleteNotFound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDeleteDataAssetNotFoundTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("asset_path"), TEXT("/Game/Temp/CortexTest_DeleteDA_NotExist_XYZ"));

	FCortexCommandResult Result = FCortexDataAssetOps::DeleteDataAsset(Params);

	TestFalse(TEXT("Delete of non-existent asset should fail"), Result.bSuccess);
	TestEqual(TEXT("Error code should be ASSET_NOT_FOUND"), Result.ErrorCode, TEXT("ASSET_NOT_FOUND"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGetDataAssetDeepPayloadTest,
	"Cortex.Data.DataAsset.GetDeepPayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexGetDataAssetDeepPayloadTest::RunTest(const FString& Parameters)
{
	const FString TestPath = TEXT("/Game/CortexTests/DA_GetDeepPayload");
	CleanupTestAsset(TestPath);
	ON_SCOPE_EXIT { CleanupTestAsset(TestPath); };

	TSharedPtr<FJsonObject> CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("class_name"), TEXT("CortexTestDataAsset"));
	CreateParams->SetStringField(TEXT("asset_path"), TestPath);
	FCortexCommandResult CreateResult = FCortexDataAssetOps::CreateDataAsset(CreateParams);
	TestTrue(TEXT("fixture asset is created"), CreateResult.bSuccess);

	UCortexTestDataAsset* Asset = LoadObject<UCortexTestDataAsset>(nullptr, TEXT("/Game/CortexTests/DA_GetDeepPayload.DA_GetDeepPayload"));
	TestNotNull(TEXT("created asset loads"), Asset);
	if (Asset == nullptr)
	{
		return false;
	}
	Asset->Nested.Visible = TEXT("visible from get");
	Asset->Nested.Internal = TEXT("internal from get");
	Asset->NestedArray.Reset();
	Asset->NestedArray.Add(Asset->Nested);
	UCortexTestDataAssetUnsupportedObject* UnsupportedObject = NewObject<UCortexTestDataAssetUnsupportedObject>(Asset);
	TScriptInterface<ICortexTestDataAssetUnsupportedInterface> UnsupportedInterface;
	UnsupportedInterface.SetObject(UnsupportedObject);
	UnsupportedInterface.SetInterface(Cast<ICortexTestDataAssetUnsupportedInterface>(UnsupportedObject));
	Asset->UnsupportedExportInterface = UnsupportedInterface;

	TSharedPtr<FJsonObject> GetParams = MakeShared<FJsonObject>();
	GetParams->SetStringField(TEXT("asset_path"), TEXT("/Game/CortexTests/DA_GetDeepPayload.DA_GetDeepPayload"));
	const FCortexCommandResult GetResult = FCortexDataAssetOps::GetDataAsset(GetParams);
	TestTrue(TEXT("get_data_asset succeeds"), GetResult.bSuccess);
	TestTrue(TEXT("get_data_asset returns payload"), GetResult.Data.IsValid());
	if (!GetResult.Data.IsValid())
	{
		return false;
	}

	TestTrue(TEXT("get_data_asset includes partial flag"), GetResult.Data->HasTypedField<EJson::Boolean>(TEXT("partial")));
	TestTrue(TEXT("get_data_asset includes issues array"), GetResult.Data->HasTypedField<EJson::Array>(TEXT("issues")));

	const TSharedPtr<FJsonObject>* Properties = nullptr;
	TestTrue(TEXT("get_data_asset includes properties object"),
		GetResult.Data->TryGetObjectField(TEXT("properties"), Properties) && Properties != nullptr && Properties->IsValid());
	if (Properties == nullptr || !Properties->IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Nested = (*Properties)->GetObjectField(TEXT("Nested"));
	TestEqual(TEXT("nested editable field is serialized"), Nested->GetStringField(TEXT("Visible")), TEXT("visible from get"));
	TestEqual(TEXT("nested non-edit field is serialized for reflected read"), Nested->GetStringField(TEXT("Internal")), TEXT("internal from get"));

	const TArray<TSharedPtr<FJsonValue>> Issues = GetResult.Data->GetArrayField(TEXT("issues"));
	TestTrue(TEXT("deep get reports unsupported interface issue"), Issues.Num() > 0);
	TestTrue(TEXT("deep get marks partial"), GetResult.Data->GetBoolField(TEXT("partial")));

	bool bFoundUnsupportedField = false;
	for (const TSharedPtr<FJsonValue>& IssueValue : Issues)
	{
		const TSharedPtr<FJsonObject>* Issue = nullptr;
		if (!IssueValue.IsValid() || !IssueValue->TryGetObject(Issue) || Issue == nullptr || !Issue->IsValid())
		{
			continue;
		}

		bFoundUnsupportedField = bFoundUnsupportedField
			|| ((*Issue)->GetStringField(TEXT("field")) == TEXT("UnsupportedExportInterface")
				&& (*Issue)->GetStringField(TEXT("code")) == TEXT("UNSUPPORTED_PROPERTY_TYPE"));
	}
	TestTrue(TEXT("deep get uses stable issue codes and field paths"), bFoundUnsupportedField);

	return true;
}

// Consumer-visible regressions for the token-efficiency list_data_assets
// selector cutover (TE04 / design 3.1): the advertised class_name selector
// must retain the existing subclass-aware behavior, and the obsolete
// class_filter name must stop filtering. Fixtures are disposable
// task-owned packages; no real project asset names.

namespace
{
	FCortexCommandRouter CreateListDataRouter()
	{
		FCortexCommandRouter Router;
		Router.RegisterDomain(
			TEXT("data"),
			TEXT("Cortex Data"),
			TEXT("1.0.1"),
			MakeShared<FCortexDataCommandHandler>());
		return Router;
	}

	void ListCleanupLoadedPackage(const FString& PackageName)
	{
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package == nullptr)
		{
			return;
		}

		TArray<UObject*> PackageObjects;
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		GetObjectsWithPackage(Package, PackageObjects, false);
#else
		GetObjectsWithPackage(Package, PackageObjects, EGetObjectsFlags::None);
#endif
		for (UObject* Object : PackageObjects)
		{
			if (Object == nullptr)
			{
				continue;
			}

			FAssetRegistryModule::AssetDeleted(Object);
			Object->ClearFlags(RF_Public | RF_Standalone);
			Object->MarkAsGarbage();
		}

		Package->ClearFlags(RF_Public | RF_Standalone);
		Package->MarkAsGarbage();
	}

	void ListDeletePackageFile(const FString& PackageName)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());

		if (IFileManager::Get().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename);
		}
	}

	double GetListDataFieldNumber(const FCortexCommandResult& Result, const FString& FieldName)
	{
		double Value = -1.0;
		if (Result.Data.IsValid())
		{
			Result.Data->TryGetNumberField(FieldName, Value);
		}
		return Value;
	}

	FString GetListDataFieldString(const FCortexCommandResult& Result, const FString& FieldName)
	{
		FString Value;
		if (Result.Data.IsValid())
		{
			Result.Data->TryGetStringField(FieldName, Value);
		}
		return Value;
	}

	/** Dedicated-package list fixture: base + derived asset in one folder. */
	class FCortexTokenListFixture
	{
	public:
		FCortexTokenListFixture()
			: RunId(FGuid::NewGuid().ToString(EGuidFormats::Digits))
		{
			Folder = FString::Printf(TEXT("/Game/Temp/CortexTokenList_%s"), *RunId);
			BaseName = FString::Printf(TEXT("CTokenBase_%s"), *RunId);
			DerivedName = FString::Printf(TEXT("CTokenDerived_%s"), *RunId);
			BasePackage = Folder + TEXT("/") + BaseName;
			DerivedPackage = Folder + TEXT("/") + DerivedName;
		}

		~FCortexTokenListFixture()
		{
			Cleanup();
		}

		void CreateAll()
		{
			CreateAndSave<UCortexTestDataAsset>(BasePackage, BaseName);
			CreateAndSave<UCortexDerivedTestDataAsset>(DerivedPackage, DerivedName);
		}

		void Cleanup()
		{
			if (CreatedPackageNames.Num() == 0)
			{
				return;
			}

			// Gathering may still hold the files opened by the previous frame's save; drain it
			// before the registry is told the packages are gone.
			IAssetRegistry& AssetRegistry =
				FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
			AssetRegistry.WaitForCompletion();
			FlushAsyncLoading();
			AssetRegistry.Tick(-1.0f);

			for (int32 Index = CreatedPackageNames.Num() - 1; Index >= 0; --Index)
			{
				ListCleanupLoadedPackage(CreatedPackageNames[Index]);
				ListDeletePackageFile(CreatedPackageNames[Index]);
			}

			CreatedPackageNames.Empty();
		}

		FString RunId;
		FString Folder;
		FString BaseName;
		FString DerivedName;
		FString BasePackage;
		FString DerivedPackage;

	private:
		template <typename AssetType>
		void CreateAndSave(const FString& PackagePath, const FString& AssetName)
		{
			UPackage* Package = CreatePackage(*PackagePath);
			AssetType* Asset = NewObject<AssetType>(
				Package,
				AssetType::StaticClass(),
				FName(*AssetName),
				RF_Public | RF_Standalone);

			FAssetRegistryModule::AssetCreated(Asset);

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			const FString Filename = FPackageName::LongPackageNameToFilename(
				PackagePath,
				FPackageName::GetAssetPackageExtension());
			UPackage::SavePackage(Package, Asset, *Filename, SaveArgs);

			CreatedPackageNames.Add(PackagePath);
		}

		TArray<FString> CreatedPackageNames;
	};

	/** Retires one list fixture across engine frames. RunTest creates and saves the fixture; this
	 *  command then yields a full engine frame so the AssetRegistry directory watcher consumes the
	 *  queued .uasset save events before the fixture is retired. Retiring inside the save frame
	 *  lets the asynchronous gather re-report a package the same frame already deleted and warn
	 *  that the deleted package was "modified on disk". */
	class FCortexListFixtureRetireCommand : public IAutomationLatentCommand
	{
	public:
		explicit FCortexListFixtureRetireCommand(TSharedPtr<FCortexTokenListFixture> InFixture)
			: Fixture(MoveTemp(InFixture))
		{
		}

		virtual bool Update() override
		{
			// Separate engine frames are required between the fixture save and its retirement.
			if (Phase == 0)
			{
				++Phase;
				return false;
			}

			Fixture.Reset();
			return true;
		}

	private:
		TSharedPtr<FCortexTokenListFixture> Fixture;
		int32 Phase = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexListDataAssetsClassNameSelectorTest,
	"Cortex.Data.DataAsset.ListClassNameSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexListDataAssetsClassNameSelectorTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FCortexTokenListFixture> FixtureOwner = MakeShared<FCortexTokenListFixture>();
	FCortexTokenListFixture& Fixture = *FixtureOwner;
	Fixture.CreateAll();

	FCortexCommandRouter Router = CreateListDataRouter();

	// The advertised class_name selector retains subclass-aware behavior.
	TSharedPtr<FJsonObject> DerivedParams = MakeShared<FJsonObject>();
	DerivedParams->SetStringField(TEXT("class_name"), TEXT("CortexDerivedTestDataAsset"));
	DerivedParams->SetStringField(TEXT("path_filter"), Fixture.Folder);
	const FCortexCommandResult DerivedResult = Router.Execute(TEXT("data.list_data_assets"), DerivedParams);
	TestTrue(TEXT("class_name derived filter succeeds"), DerivedResult.bSuccess);
	TestEqual(TEXT("derived class_name returns only the derived asset"),
		static_cast<int32>(GetListDataFieldNumber(DerivedResult, TEXT("count"))), 1);
	TestEqual(TEXT("derived class_name resolves the class"),
		GetListDataFieldString(DerivedResult, TEXT("resolved_class")), TEXT("CortexDerivedTestDataAsset"));

	TSharedPtr<FJsonObject> BaseParams = MakeShared<FJsonObject>();
	BaseParams->SetStringField(TEXT("class_name"), TEXT("CortexTestDataAsset"));
	BaseParams->SetStringField(TEXT("path_filter"), Fixture.Folder);
	const FCortexCommandResult BaseResult = Router.Execute(TEXT("data.list_data_assets"), BaseParams);
	TestTrue(TEXT("class_name base filter succeeds"), BaseResult.bSuccess);
	TestEqual(TEXT("base class_name keeps subclass-aware behavior"),
		static_cast<int32>(GetListDataFieldNumber(BaseResult, TEXT("count"))), 2);
	TestEqual(TEXT("base class_name resolves the class"),
		GetListDataFieldString(BaseResult, TEXT("resolved_class")), TEXT("CortexTestDataAsset"));

	ADD_LATENT_AUTOMATION_COMMAND(FCortexListFixtureRetireCommand(FixtureOwner));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexListDataAssetsClassFilterRemovedTest,
	"Cortex.Data.DataAsset.ListClassFilterRemoved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexListDataAssetsClassFilterRemovedTest::RunTest(const FString& Parameters)
{
	TSharedPtr<FCortexTokenListFixture> FixtureOwner = MakeShared<FCortexTokenListFixture>();
	FCortexTokenListFixture& Fixture = *FixtureOwner;
	Fixture.CreateAll();

	FCortexCommandRouter Router = CreateListDataRouter();

	// The obsolete class_filter selector must no longer filter list_data_assets.
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("class_filter"), TEXT("NonExistentClass_CortexTest"));
	Params->SetStringField(TEXT("path_filter"), Fixture.Folder);
	const FCortexCommandResult Result = Router.Execute(TEXT("data.list_data_assets"), Params);
	TestTrue(TEXT("list_data_assets succeeds"), Result.bSuccess);
	TestEqual(TEXT("obsolete class_filter no longer filters"),
		static_cast<int32>(GetListDataFieldNumber(Result, TEXT("count"))), 2);
	TestTrue(TEXT("obsolete class_filter does not resolve a class"),
		GetListDataFieldString(Result, TEXT("resolved_class")).IsEmpty());

	ADD_LATENT_AUTOMATION_COMMAND(FCortexListFixtureRetireCommand(FixtureOwner));
	return true;
}
