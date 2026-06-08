#include "CoreMinimal.h"
#include "CortexCommandRouter.h"
#include "CortexDataCommandHandler.h"
#include "CortexTypes.h"
#include "Tests/CortexDataLocalizationTestTypes.h"
#include "Tests/CortexTestDataAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/CompositeDataTable.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectHash.h"

namespace
{
	const TCHAR* ExportTestRoot = TEXT("/Game/CortexExportTests");

	FCortexCommandRouter CreateDataExportTestRouter()
	{
		FCortexCommandRouter Router;
		Router.RegisterDomain(
			TEXT("data"),
			TEXT("Cortex Data"),
			TEXT("1.0.1"),
			MakeShared<FCortexDataCommandHandler>());
		return Router;
	}

	FCortexDataLocalizationTestRow MakeExportRow(const FString& Title, const FString& StepText)
	{
		FCortexDataLocalizationTestRow Row;
		Row.Title = FText::FromString(Title);

		FCortexDataLocalizationStepTestRow Step;
		Step.Description = FText::FromString(StepText);
		Row.Steps.Add(Step);

		return Row;
	}

	FString PackagePathForAsset(const FString& RunId, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s/%s"), ExportTestRoot, *RunId, *AssetName);
	}

	bool SupportedCommandNamesContain(const TArray<FCortexCommandInfo>& Commands, const FString& CommandName)
	{
		for (const FCortexCommandInfo& Command : Commands)
		{
			if (Command.Name == CommandName)
			{
				return true;
			}
		}

		return false;
	}

	void DeletePackageFile(const FString& PackageName)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());

		if (IFileManager::Get().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename);
		}
	}

	void CleanupLoadedPackage(const FString& PackageName)
	{
		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			TArray<UObject*> PackageObjects;
			GetObjectsWithPackage(Package, PackageObjects, false);
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
	}

	void CleanupPackageByName(const FString& PackageName)
	{
		CleanupLoadedPackage(PackageName);
		DeletePackageFile(PackageName);
	}

	class FCortexDataExportTestFixture
	{
	public:
		FCortexDataExportTestFixture()
			: RunId(FString::Printf(TEXT("Run_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FCortexDataExportTestFixture()
		{
			Cleanup();
		}

		UDataTable* CreateRegularDataTable()
		{
			UDataTable* Table = CreateAsset<UDataTable>(TEXT("DT_CortexExportRows"));
			if (Table == nullptr)
			{
				return nullptr;
			}

			Table->RowStruct = FCortexDataLocalizationTestRow::StaticStruct();

			Table->AddRow(TEXT("zeta"), MakeExportRow(TEXT("Zeta"), TEXT("Third inserted")));
			Table->AddRow(TEXT("alpha"), MakeExportRow(TEXT("Alpha"), TEXT("Second inserted")));
			Table->AddRow(TEXT("middle"), MakeExportRow(TEXT("Middle"), TEXT("First inserted")));

			return Table;
		}

		UCompositeDataTable* CreateCompositeDataTable()
		{
			UDataTable* ParentA = CreateAsset<UDataTable>(TEXT("DT_CortexExportParentA"));
			if (ParentA == nullptr)
			{
				return nullptr;
			}

			ParentA->RowStruct = FCortexDataLocalizationTestRow::StaticStruct();
			ParentA->AddRow(TEXT("shared"), MakeExportRow(TEXT("Base Shared"), TEXT("Base parent")));
			ParentA->AddRow(TEXT("alpha"), MakeExportRow(TEXT("Alpha Parent"), TEXT("Base only")));

			UDataTable* ParentB = CreateAsset<UDataTable>(TEXT("DT_CortexExportParentB"));
			if (ParentB == nullptr)
			{
				return nullptr;
			}

			ParentB->RowStruct = FCortexDataLocalizationTestRow::StaticStruct();
			ParentB->AddRow(TEXT("shared"), MakeExportRow(TEXT("Override Shared"), TEXT("Override parent")));
			ParentB->AddRow(TEXT("beta"), MakeExportRow(TEXT("Beta Parent"), TEXT("Override only")));

			UCompositeDataTable* Composite = CreateAsset<UCompositeDataTable>(TEXT("DT_CortexExportComposite"));
			if (Composite == nullptr)
			{
				return nullptr;
			}

			Composite->RowStruct = FCortexDataLocalizationTestRow::StaticStruct();

			TArray<UDataTable*> Parents;
			Parents.Add(ParentA);
			Parents.Add(ParentB);
			Composite->AppendParentTables(Parents);

			return Composite;
		}

		UStringTable* CreateStringTable()
		{
			UStringTable* Table = CreateAsset<UStringTable>(TEXT("ST_CortexExportText"));
			if (Table == nullptr)
			{
				return nullptr;
			}

			Table->GetMutableStringTable()->SetNamespace(TEXT("CortexExportTests"));
			Table->GetMutableStringTable()->SetSourceString(TEXT("zeta.key"), TEXT("Zeta text"));
			Table->GetMutableStringTable()->SetSourceString(TEXT("alpha.key"), TEXT("Alpha text"));
			Table->GetMutableStringTable()->SetSourceString(TEXT("middle.key"), TEXT("Middle text"));
			return Table;
		}

		UCortexTestDataAsset* CreateDataAsset()
		{
			UCortexTestDataAsset* Asset = CreateAsset<UCortexTestDataAsset>(TEXT("DA_CortexExportFixture"));
			if (Asset == nullptr)
			{
				return nullptr;
			}

			Asset->TestProperty = TEXT("Editable export value");
			Asset->TestNumber = 42;
			Asset->ExportTransientProperty = TEXT("Transient value");
#if WITH_EDITORONLY_DATA
			Asset->ExportEditorOnlyProperty = TEXT("Editor-only value");
#endif
			Asset->ExportInternalProperty = TEXT("Internal value");
			return Asset;
		}

		FString MakeSavedOutputPath(const FString& FileName) const
		{
			return FPaths::Combine(GetSavedRunDir(), FileName);
		}

		FString MakeSavedOutputDir(const FString& DirName) const
		{
			return FPaths::Combine(GetSavedRunDir(), DirName);
		}

		bool TryReadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError) const
		{
			FString Contents;
			if (!FFileHelper::LoadFileToString(Contents, *FilePath))
			{
				OutError = FString::Printf(TEXT("Could not read JSON file: %s"), *FilePath);
				return false;
			}

			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
			if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
			{
				OutError = FString::Printf(TEXT("Could not parse JSON file: %s"), *FilePath);
				return false;
			}

			return true;
		}

		void Cleanup()
		{
			IFileManager::Get().DeleteDirectory(*GetSavedRunDir(), false, true);

			for (int32 Index = CreatedPackageNames.Num() - 1; Index >= 0; --Index)
			{
				CleanupPackageByName(CreatedPackageNames[Index]);
			}
			CreatedPackageNames.Empty();
		}

	private:
		FString GetSavedRunDir() const
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CortexExportTests"), RunId);
		}

		template <typename AssetType>
		AssetType* CreateAsset(const FString& AssetName)
		{
			const FString PackageName = PackagePathForAsset(RunId, AssetName);
			if (FindPackage(nullptr, *PackageName) != nullptr || FPackageName::DoesPackageExist(PackageName))
			{
				return nullptr;
			}

			UPackage* Package = CreatePackage(*PackageName);
			AssetType* Asset = NewObject<AssetType>(
				Package,
				AssetType::StaticClass(),
				FName(*AssetName),
				RF_Public | RF_Standalone);

			FAssetRegistryModule::AssetCreated(Asset);

			CreatedPackageNames.Add(PackageName);
			return Asset;
		}

		FString RunId;
		TArray<FString> CreatedPackageNames;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataExportFixtureSmokeTest,
	"Cortex.Data.Export.FixtureSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataExportFixtureSmokeTest::RunTest(const FString& Parameters)
{
	FCortexDataExportTestFixture Fixture;

	UDataTable* RegularTable = Fixture.CreateRegularDataTable();
	TestNotNull(TEXT("regular DataTable fixture is created"), RegularTable);
	if (RegularTable != nullptr)
	{
		TestEqual(TEXT("regular fixture has deterministic row count"), RegularTable->GetRowMap().Num(), 3);
	}

	UCompositeDataTable* CompositeTable = Fixture.CreateCompositeDataTable();
	TestNotNull(TEXT("CompositeDataTable fixture is created"), CompositeTable);
	if (CompositeTable != nullptr)
	{
		const uint8* SharedRowData = CompositeTable->FindRowUnchecked(TEXT("shared"));
		TestNotNull(TEXT("composite fixture has overridden shared row"), SharedRowData);
		if (SharedRowData != nullptr)
		{
			const FCortexDataLocalizationTestRow* SharedRow = reinterpret_cast<const FCortexDataLocalizationTestRow*>(SharedRowData);
			TestEqual(TEXT("later composite parent overrides earlier parent"), SharedRow->Title.ToString(), TEXT("Override Shared"));
		}
	}

	UStringTable* StringTable = Fixture.CreateStringTable();
	TestNotNull(TEXT("StringTable fixture is created"), StringTable);
	if (StringTable != nullptr)
	{
		FString SourceString;
		const bool bFoundAlpha = StringTable->GetStringTable()->GetSourceString(TEXT("alpha.key"), SourceString);
		TestTrue(TEXT("StringTable fixture contains out-of-order alpha key"), bFoundAlpha);
		TestEqual(TEXT("StringTable alpha key has deterministic value"), SourceString, TEXT("Alpha text"));
	}

	UCortexTestDataAsset* DataAsset = Fixture.CreateDataAsset();
	TestNotNull(TEXT("DataAsset fixture is created"), DataAsset);
	if (DataAsset != nullptr)
	{
		TestEqual(TEXT("DataAsset editable string is populated"), DataAsset->TestProperty, TEXT("Editable export value"));
		TestEqual(TEXT("DataAsset editable number is populated"), DataAsset->TestNumber, 42);
		TestEqual(TEXT("DataAsset transient blocked field is populated"), DataAsset->ExportTransientProperty, TEXT("Transient value"));
#if WITH_EDITORONLY_DATA
		TestEqual(TEXT("DataAsset editor-only blocked field is populated"), DataAsset->ExportEditorOnlyProperty, TEXT("Editor-only value"));
#endif
		TestEqual(TEXT("DataAsset internal blocked field is populated"), DataAsset->ExportInternalProperty, TEXT("Internal value"));
	}

	const FString ProbePath = Fixture.MakeSavedOutputPath(TEXT("probe.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ProbePath), true);
	TestTrue(TEXT("probe JSON file writes to Saved/CortexExportTests"),
		FFileHelper::SaveStringToFile(TEXT("{\"ok\":true}"), *ProbePath));

	TSharedPtr<FJsonObject> ParsedProbe;
	FString ParseError;
	TestTrue(TEXT("fixture helper parses written JSON from disk"), Fixture.TryReadJsonFile(ProbePath, ParsedProbe, ParseError));
	if (!ParseError.IsEmpty())
	{
		AddError(ParseError);
	}
	if (ParsedProbe.IsValid())
	{
		TestTrue(TEXT("parsed probe JSON contains expected bool"), ParsedProbe->GetBoolField(TEXT("ok")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataExportCommandsRegisteredTest,
	"Cortex.Data.Export.CommandsRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataExportCommandsRegisteredTest::RunTest(const FString& Parameters)
{
	FCortexDataCommandHandler Handler;
	const TArray<FCortexCommandInfo> Commands = Handler.GetSupportedCommands();

	TestTrue(TEXT("export_datatable_json is advertised"),
		SupportedCommandNamesContain(Commands, TEXT("export_datatable_json")));
	TestTrue(TEXT("export_string_table_json is advertised"),
		SupportedCommandNamesContain(Commands, TEXT("export_string_table_json")));
	TestTrue(TEXT("export_data_assets_json is advertised"),
		SupportedCommandNamesContain(Commands, TEXT("export_data_assets_json")));
	TestTrue(TEXT("export_bulk_json is advertised"),
		SupportedCommandNamesContain(Commands, TEXT("export_bulk_json")));

	FCortexCommandRouter Router = CreateDataExportTestRouter();
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("table_path"), TEXT("/Game/CortexExportTests/Missing.Missing"));
	Params->SetStringField(TEXT("out_path"), FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CortexExportTests"), TEXT("registered.json")));

	const FCortexCommandResult Result = Router.Execute(TEXT("data.export_datatable_json"), Params);
	TestFalse(TEXT("export_datatable_json is registered and validates the missing table"), Result.bSuccess);
	TestEqual(TEXT("registered command returns domain error, not UnknownCommand"), Result.ErrorCode, CortexErrorCodes::TableNotFound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataExportPathSafetyTest,
	"Cortex.Data.Export.PathSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataExportPathSafetyTest::RunTest(const FString& Parameters)
{
	FCortexDataExportTestFixture Fixture;
	FCortexCommandRouter Router = CreateDataExportTestRouter();

	auto ExecuteDatatableExport = [&Router](const FString& OutPath)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("table_path"), TEXT("/Game/CortexExportTests/Missing.Missing"));
		Params->SetStringField(TEXT("out_path"), OutPath);
		return Router.Execute(TEXT("data.export_datatable_json"), Params);
	};

	const FString TraversalBase = Fixture.MakeSavedOutputDir(TEXT("TraversalBase"));
	const FString TraversalPath = FPaths::Combine(TraversalBase, TEXT(".."), TEXT("TraversalEscape"), TEXT("out.json"));
	const FString UnexpectedTraversalDirectory = FPaths::Combine(FPaths::GetPath(TraversalBase), TEXT("TraversalEscape"));
	const FCortexCommandResult TraversalResult = ExecuteDatatableExport(TraversalPath);
	TestFalse(TEXT("Traversal output paths are rejected"), TraversalResult.bSuccess);
	TestEqual(TEXT("Traversal rejection uses InvalidField"), TraversalResult.ErrorCode, CortexErrorCodes::InvalidField);
	TestFalse(TEXT("Traversal rejection creates no directory"), IFileManager::Get().DirectoryExists(*UnexpectedTraversalDirectory));

	FString ProjectRootForSibling = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectRootForSibling);
	const FString SiblingEscapePath = ProjectRootForSibling + TEXT("_Evil/out.json");
	const FCortexCommandResult SiblingResult = ExecuteDatatableExport(SiblingEscapePath);
	TestFalse(TEXT("Sibling-prefix output paths are rejected"), SiblingResult.bSuccess);
	TestEqual(TEXT("Sibling-prefix rejection uses InvalidField"), SiblingResult.ErrorCode, CortexErrorCodes::InvalidField);

	const FCortexCommandResult DevicePathResult = ExecuteDatatableExport(TEXT("\\\\?\\C:\\CortexExportTests\\out.json"));
	TestFalse(TEXT("Win32 device output paths are rejected"), DevicePathResult.bSuccess);
	TestEqual(TEXT("Win32 device rejection uses InvalidField"), DevicePathResult.ErrorCode, CortexErrorCodes::InvalidField);

	const FCortexCommandResult DotDevicePathResult = ExecuteDatatableExport(TEXT("\\\\.\\C:\\CortexExportTests\\out.json"));
	TestFalse(TEXT("Win32 dot-device output paths are rejected"), DotDevicePathResult.bSuccess);
	TestEqual(TEXT("Win32 dot-device rejection uses InvalidField"), DotDevicePathResult.ErrorCode, CortexErrorCodes::InvalidField);

	const FCortexCommandResult UncPathResult = ExecuteDatatableExport(TEXT("\\\\server\\share\\out.json"));
	TestFalse(TEXT("UNC output paths are rejected"), UncPathResult.bSuccess);
	TestEqual(TEXT("UNC path rejection uses InvalidField"), UncPathResult.ErrorCode, CortexErrorCodes::InvalidField);

	const FCortexCommandResult DriveRelativePathResult = ExecuteDatatableExport(TEXT("C:relative\\out.json"));
	TestFalse(TEXT("Drive-relative output paths are rejected"), DriveRelativePathResult.bSuccess);
	TestEqual(TEXT("Drive-relative path rejection uses InvalidField"), DriveRelativePathResult.ErrorCode, CortexErrorCodes::InvalidField);

	const FString ExistingDirectoryTarget = Fixture.MakeSavedOutputDir(TEXT("ExistingDirectoryTarget"));
	IFileManager::Get().MakeDirectory(*ExistingDirectoryTarget, true);
	const FCortexCommandResult ExistingDirectoryResult = ExecuteDatatableExport(ExistingDirectoryTarget);
	TestFalse(TEXT("Existing directory output targets are rejected"), ExistingDirectoryResult.bSuccess);
	TestEqual(TEXT("Existing directory rejection uses InvalidField"), ExistingDirectoryResult.ErrorCode, CortexErrorCodes::InvalidField);

	const FString ExternalTargetDir = FPaths::Combine(FPaths::ProjectIntermediateDir(), TEXT("CortexExportSymlinkTarget"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	const FString LinkDir = Fixture.MakeSavedOutputDir(TEXT("SymlinkParent"));
	IFileManager::Get().MakeDirectory(*ExternalTargetDir, true);
	ON_SCOPE_EXIT
	{
		IFileManager::Get().DeleteDirectory(*ExternalTargetDir, false, true);
	};

#if PLATFORM_WINDOWS
	FString StdOut;
	FString StdErr;
	int32 MklinkExitCode = INDEX_NONE;
	const FString MklinkArgs = FString::Printf(TEXT("/C mklink /J \"%s\" \"%s\""), *LinkDir, *ExternalTargetDir);
	const bool bMklinkStarted = FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *MklinkArgs, &MklinkExitCode, &StdOut, &StdErr);
	if (!bMklinkStarted || MklinkExitCode != 0 || !IFileManager::Get().DirectoryExists(*LinkDir))
	{
		AddInfo(FString::Printf(TEXT("Skipping symlink/junction escape test; mklink /J failed with code %d: %s %s"), MklinkExitCode, *StdOut, *StdErr));
		return true;
	}
#else
	AddInfo(TEXT("Skipping symlink/junction escape test on this platform"));
	return true;
#endif

	const FCortexCommandResult SymlinkResult = ExecuteDatatableExport(FPaths::Combine(LinkDir, TEXT("NewDir"), TEXT("out.json")));
	TestFalse(TEXT("Symlink/junction parent escapes are rejected"), SymlinkResult.bSuccess);
	TestEqual(TEXT("Symlink/junction rejection uses InvalidField"), SymlinkResult.ErrorCode, CortexErrorCodes::InvalidField);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataExportCanonicalWriterTest,
	"Cortex.Data.Export.CanonicalWriter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataExportCanonicalWriterTest::RunTest(const FString& Parameters)
{
	FCortexDataExportTestFixture Fixture;
	UDataTable* RegularTable = Fixture.CreateRegularDataTable();
	TestNotNull(TEXT("regular DataTable fixture is created"), RegularTable);
	if (RegularTable == nullptr)
	{
		return true;
	}

	FCortexCommandRouter Router = CreateDataExportTestRouter();
	const FString FirstOutPath = Fixture.MakeSavedOutputPath(TEXT("canonical-a.json"));
	const FString SecondOutPath = Fixture.MakeSavedOutputPath(TEXT("canonical-b.json"));

	auto ExecuteDatatableExport = [&Router, RegularTable](const FString& OutPath)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("table_path"), RegularTable->GetPathName());
		Params->SetStringField(TEXT("out_path"), OutPath);
		return Router.Execute(TEXT("data.export_datatable_json"), Params);
	};

	const FCortexCommandResult FirstResult = ExecuteDatatableExport(FirstOutPath);
	TestTrue(TEXT("DataTable skeleton export succeeds"), FirstResult.bSuccess);
	const FCortexCommandResult SecondResult = ExecuteDatatableExport(SecondOutPath);
	TestTrue(TEXT("Repeated DataTable skeleton export succeeds"), SecondResult.bSuccess);

	FString FirstContents;
	FString SecondContents;
	TestTrue(TEXT("first skeleton export writes JSON"), FFileHelper::LoadFileToString(FirstContents, *FirstOutPath));
	TestTrue(TEXT("second skeleton export writes JSON"), FFileHelper::LoadFileToString(SecondContents, *SecondOutPath));

	if (!FirstContents.IsEmpty() && !SecondContents.IsEmpty())
	{
		TestEqual(TEXT("canonical skeleton exports are byte-for-byte stable"), FirstContents, SecondContents);

		const int32 ArrayOrderIndex = FirstContents.Find(TEXT("\"array_order\""));
		const int32 SummaryIndex = FirstContents.Find(TEXT("\"summary\""));
		const int32 ZMarkerIndex = FirstContents.Find(TEXT("\"z_marker\""));
		TestTrue(TEXT("canonical writer sorts root keys"), ArrayOrderIndex != INDEX_NONE && ArrayOrderIndex < SummaryIndex && SummaryIndex < ZMarkerIndex);

		const int32 AlphaIndex = FirstContents.Find(TEXT("\"alpha\""));
		const int32 BetaIndex = FirstContents.Find(TEXT("\"beta\""));
		TestTrue(TEXT("canonical writer sorts nested object keys"), AlphaIndex != INDEX_NONE && AlphaIndex < BetaIndex);

		const int32 FirstIndex = FirstContents.Find(TEXT("\"first\""));
		const int32 SecondIndex = FirstContents.Find(TEXT("\"second\""));
		TestTrue(TEXT("canonical writer preserves array order"), FirstIndex != INDEX_NONE && FirstIndex < SecondIndex);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataExportBulkPathSafetyTest,
	"Cortex.Data.Export.BulkPathSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataExportBulkPathSafetyTest::RunTest(const FString& Parameters)
{
	FCortexDataExportTestFixture Fixture;
	FCortexCommandRouter Router = CreateDataExportTestRouter();

	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("type"), TEXT("datatable"));
	Item->SetStringField(TEXT("table_path"), TEXT("/Game/CortexExportTests/Missing.Missing"));
	Item->SetStringField(TEXT("out_path"), Fixture.MakeSavedOutputPath(TEXT("absolute-item.json")));

	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Add(MakeShared<FJsonValueObject>(Item));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("out_dir"), Fixture.MakeSavedOutputDir(TEXT("BulkOut")));
	Params->SetArrayField(TEXT("items"), Items);

	const FCortexCommandResult Result = Router.Execute(TEXT("data.export_bulk_json"), Params);
	TestFalse(TEXT("Bulk item absolute output paths are rejected"), Result.bSuccess);
	TestEqual(TEXT("Bulk item absolute path rejection uses InvalidField"), Result.ErrorCode, CortexErrorCodes::InvalidField);

	return true;
}
