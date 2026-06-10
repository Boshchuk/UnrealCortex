#include "CoreMinimal.h"
#include "CortexCommandRouter.h"
#include "CortexDataCommandHandler.h"
#include "CortexTypes.h"
#include "Tests/CortexDataLocalizationTestTypes.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UObjectHash.h"

namespace
{
	const TCHAR* JsonDiffTestRoot = TEXT("/Game/CortexJsonDiffTests");

	FCortexCommandRouter CreateDataJsonDiffTestRouter()
	{
		FCortexCommandRouter Router;
		Router.RegisterDomain(
			TEXT("data"),
			TEXT("Cortex Data"),
			TEXT("1.0.1"),
			MakeShared<FCortexDataCommandHandler>());
		return Router;
	}

	FCortexDataLocalizationTestRow MakeRow(const FString& Title, const FString& StepText)
	{
		FCortexDataLocalizationTestRow Row;
		Row.Title = FText::FromString(Title);
		Row.row_name = Title;

		FCortexDataLocalizationStepTestRow Step;
		Step.Description = FText::FromString(StepText);
		Row.Steps.Add(Step);
		return Row;
	}

	FString PackagePathForAsset(const FString& RunId, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s/%s"), JsonDiffTestRoot, *RunId, *AssetName);
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

	class FCortexDataJsonDiffTestFixture
	{
	public:
		FCortexDataJsonDiffTestFixture()
			: RunId(FString::Printf(TEXT("Run_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FCortexDataJsonDiffTestFixture()
		{
			Cleanup();
		}

		UDataTable* CreateRegularDataTable(const FString& AssetName)
		{
			UDataTable* Table = CreateAsset<UDataTable>(AssetName);
			if (Table == nullptr)
			{
				return nullptr;
			}

			Table->RowStruct = FCortexDataLocalizationTestRow::StaticStruct();
			Table->AddRow(TEXT("alpha"), MakeRow(TEXT("Alpha"), TEXT("First step")));
			Table->AddRow(TEXT("beta"), MakeRow(TEXT("Beta"), TEXT("Second step")));
			return Table;
		}

		void UpdateRowTitle(UDataTable* Table, const FName RowName, const FString& NewTitle) const
		{
			if (Table == nullptr)
			{
				return;
			}

			if (FCortexDataLocalizationTestRow* Row = Table->FindRow<FCortexDataLocalizationTestRow>(RowName, TEXT("JsonDiffTest")))
			{
				Row->Title = FText::FromString(NewTitle);
			}
		}

		UStringTable* CreateStringTable(const FString& AssetName)
		{
			UStringTable* Table = CreateAsset<UStringTable>(AssetName);
			if (Table == nullptr)
			{
				return nullptr;
			}

			Table->GetMutableStringTable()->SetNamespace(TEXT("CortexJsonDiffTests"));
			Table->GetMutableStringTable()->SetSourceString(TEXT("alpha.key"), TEXT("Alpha text"));
			Table->GetMutableStringTable()->SetSourceString(TEXT("beta.key"), TEXT("Beta text"));
			return Table;
		}

		void UpdateStringTableValue(UStringTable* Table, const FString& Key, const FString& Value) const
		{
			if (Table != nullptr)
			{
				Table->GetMutableStringTable()->SetSourceString(Key, Value);
			}
		}

		FString ExportDatatableJson(UDataTable* Table, const FString& FileName) const
		{
			const FString OutputPath = MakeSavedPath(FileName);
			TSharedPtr<FJsonObject> ParamsObject = MakeShared<FJsonObject>();
			ParamsObject->SetStringField(TEXT("table_path"), Table != nullptr ? Table->GetPathName() : TEXT(""));
			ParamsObject->SetStringField(TEXT("out_path"), OutputPath);
			const FCortexCommandResult Result = CreateDataJsonDiffTestRouter().Execute(TEXT("data.export_datatable_json"), ParamsObject);
			return Result.bSuccess ? OutputPath : TEXT("");
		}

		FString ExportStringTableJson(UStringTable* Table, const FString& FileName) const
		{
			const FString OutputPath = MakeSavedPath(FileName);
			TSharedPtr<FJsonObject> ParamsObject = MakeShared<FJsonObject>();
			ParamsObject->SetStringField(TEXT("string_table_path"), Table != nullptr ? Table->GetPathName() : TEXT(""));
			ParamsObject->SetStringField(TEXT("out_path"), OutputPath);
			const FCortexCommandResult Result = CreateDataJsonDiffTestRouter().Execute(TEXT("data.export_string_table_json"), ParamsObject);
			return Result.bSuccess ? OutputPath : TEXT("");
		}

		FString WriteJsonFile(const FString& FileName, const FString& Contents) const
		{
			const FString OutputPath = MakeSavedPath(FileName);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
			return FFileHelper::SaveStringToFile(Contents, *OutputPath) ? OutputPath : TEXT("");
		}

		FCortexCommandResult CompareJson(
			const FString& LeftPath,
			const FString& RightPath,
			const FString& ReportPath,
			const FString& Mode,
			const FString& KeyField = TEXT("")) const
		{
			TSharedPtr<FJsonObject> ParamsObject = MakeShared<FJsonObject>();
			ParamsObject->SetStringField(TEXT("left_path"), LeftPath);
			ParamsObject->SetStringField(TEXT("right_path"), RightPath);
			ParamsObject->SetStringField(TEXT("report_path"), ReportPath);
			if (!Mode.IsEmpty())
			{
				ParamsObject->SetStringField(TEXT("mode"), Mode);
			}
			if (!KeyField.IsEmpty())
			{
				ParamsObject->SetStringField(TEXT("key_field"), KeyField);
			}
			return CreateDataJsonDiffTestRouter().Execute(TEXT("data.compare_data_json"), ParamsObject);
		}

		FCortexCommandResult CompareJsonWithoutMode(
			const FString& LeftPath,
			const FString& RightPath,
			const FString& ReportPath) const
		{
			return CompareJson(LeftPath, RightPath, ReportPath, TEXT(""));
		}

		FString MakeSavedPath(const FString& FileName) const
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CortexJsonDiffTests"), RunId, FileName);
		}

		bool FileExists(const FString& FilePath) const
		{
			return IFileManager::Get().FileExists(*FilePath);
		}

	private:
		void Cleanup()
		{
			IFileManager::Get().DeleteDirectory(*FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CortexJsonDiffTests"), RunId), false, true);
			for (int32 Index = CreatedPackageNames.Num() - 1; Index >= 0; --Index)
			{
				CleanupPackageByName(CreatedPackageNames[Index]);
			}
			CreatedPackageNames.Empty();
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
	FCortexDataJsonDiffCommandsRegisteredTest,
	"Cortex.Data.JsonDiff.CommandsRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffDatatableAutoCanonicalTest,
	"Cortex.Data.JsonDiff.Datatable.AutoCanonical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffAutoRejectsGenericWrappersTest,
	"Cortex.Data.JsonDiff.AutoRejectsGenericWrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffModeOmittedDefaultsToAutoTest,
	"Cortex.Data.JsonDiff.Datatable.ModeOmittedDefaultsToAuto",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffStringTableCanonicalTest,
	"Cortex.Data.JsonDiff.StringTable.Canonical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffDataAssetsCanonicalTest,
	"Cortex.Data.JsonDiff.DataAssets.Canonical",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffExplicitWrapperModesTest,
	"Cortex.Data.JsonDiff.External.Wrappers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffExplicitKeyFieldTest,
	"Cortex.Data.JsonDiff.External.KeyField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffDuplicateKeysFailTest,
	"Cortex.Data.JsonDiff.External.DuplicateKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataJsonDiffCommandsRegisteredTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCortexDataCommandHandler Handler;
	const TArray<FCortexCommandInfo> Commands = Handler.GetSupportedCommands();

	TestTrue(
		TEXT("compare_data_json is advertised"),
		SupportedCommandNamesContain(Commands, TEXT("compare_data_json")));

	FCortexCommandRouter Router = CreateDataJsonDiffTestRouter();

	TSharedPtr<FJsonObject> ParamsObject = MakeShared<FJsonObject>();
	ParamsObject->SetStringField(TEXT("left_path"), TEXT("Saved/CortexReports/left.json"));
	ParamsObject->SetStringField(TEXT("right_path"), TEXT("Saved/CortexReports/right.json"));
	ParamsObject->SetStringField(TEXT("report_path"), TEXT("Saved/CortexReports/diff.json"));

	const FCortexCommandResult Result = Router.Execute(TEXT("data.compare_data_json"), ParamsObject);
	TestNotEqual(TEXT("command is routed, not unknown"), Result.ErrorCode, CortexErrorCodes::UnknownCommand);

	return true;
}

bool FCortexDataJsonDiffDatatableAutoCanonicalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	UDataTable* LeftTable = Fixture.CreateRegularDataTable(TEXT("DT_Left"));
	UDataTable* RightTable = Fixture.CreateRegularDataTable(TEXT("DT_Right"));
	Fixture.UpdateRowTitle(RightTable, TEXT("alpha"), TEXT("Alpha changed"));

	const FCortexCommandResult Result = Fixture.CompareJson(
		Fixture.ExportDatatableJson(LeftTable, TEXT("left.json")),
		Fixture.ExportDatatableJson(RightTable, TEXT("right.json")),
		Fixture.MakeSavedPath(TEXT("report.json")),
		TEXT("auto"));

	TestTrue(TEXT("canonical datatable compare succeeds"), Result.bSuccess);
	TestTrue(TEXT("canonical datatable compare returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("mode resolves to datatable_rows"), Result.Data->GetStringField(TEXT("mode")), TEXT("datatable_rows"));
	TestEqual(
		TEXT("changed count is one"),
		static_cast<int32>(Result.Data->GetObjectField(TEXT("counts"))->GetNumberField(TEXT("changed"))),
		1);
	return true;
}

bool FCortexDataJsonDiffAutoRejectsGenericWrappersTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	const FString LeftPath = Fixture.WriteJsonFile(TEXT("left.json"), TEXT(R"({"items":[{"row_name":"alpha","Title":"Old"}]})"));
	const FString RightPath = Fixture.WriteJsonFile(TEXT("right.json"), TEXT(R"({"items":[{"row_name":"alpha","Title":"New"}]})"));
	const FString ReportPath = Fixture.MakeSavedPath(TEXT("report.json"));

	const FCortexCommandResult Result = Fixture.CompareJson(LeftPath, RightPath, ReportPath, TEXT("auto"));
	TestFalse(TEXT("generic wrapper auto detection fails"), Result.bSuccess);
	TestEqual(TEXT("generic wrapper auto failure uses InvalidOperation"), Result.ErrorCode, CortexErrorCodes::InvalidOperation);
	TestFalse(TEXT("no report is written on auto-detection failure"), Fixture.FileExists(ReportPath));
	return true;
}

bool FCortexDataJsonDiffModeOmittedDefaultsToAutoTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	UDataTable* LeftTable = Fixture.CreateRegularDataTable(TEXT("DT_DefaultLeft"));
	UDataTable* RightTable = Fixture.CreateRegularDataTable(TEXT("DT_DefaultRight"));

	const FCortexCommandResult Result = Fixture.CompareJsonWithoutMode(
		Fixture.ExportDatatableJson(LeftTable, TEXT("left.json")),
		Fixture.ExportDatatableJson(RightTable, TEXT("right.json")),
		Fixture.MakeSavedPath(TEXT("report.json")));

	TestTrue(TEXT("omitted mode defaults to auto"), Result.bSuccess);
	TestTrue(TEXT("omitted mode returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("mode resolves to datatable_rows"), Result.Data->GetStringField(TEXT("mode")), TEXT("datatable_rows"));
	return true;
}

bool FCortexDataJsonDiffStringTableCanonicalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	UStringTable* LeftTable = Fixture.CreateStringTable(TEXT("ST_Left"));
	UStringTable* RightTable = Fixture.CreateStringTable(TEXT("ST_Right"));
	Fixture.UpdateStringTableValue(RightTable, TEXT("alpha.key"), TEXT("Alpha changed"));

	const FCortexCommandResult Result = Fixture.CompareJson(
		Fixture.ExportStringTableJson(LeftTable, TEXT("left.json")),
		Fixture.ExportStringTableJson(RightTable, TEXT("right.json")),
		Fixture.MakeSavedPath(TEXT("report.json")),
		TEXT("auto"));

	TestTrue(TEXT("canonical string table compare succeeds"), Result.bSuccess);
	TestTrue(TEXT("canonical string table compare returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("mode resolves to string_table_entries"), Result.Data->GetStringField(TEXT("mode")), TEXT("string_table_entries"));
	return true;
}

bool FCortexDataJsonDiffDataAssetsCanonicalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	const FString LeftJson = TEXT(R"({"data_assets":[{"path":"/Game/Data/DA_Quest.DA_Quest","name":"DA_Quest","asset_class":"CortexTestDataAsset","properties":{"TestNumber":26}}]})");
	const FString RightJson = TEXT(R"({"data_assets":[{"path":"/Game/Data/DA_Quest.DA_Quest","name":"DA_Quest","asset_class":"CortexTestDataAsset","properties":{"TestNumber":99}}]})");

	const FCortexCommandResult Result = Fixture.CompareJson(
		Fixture.WriteJsonFile(TEXT("left.json"), LeftJson),
		Fixture.WriteJsonFile(TEXT("right.json"), RightJson),
		Fixture.MakeSavedPath(TEXT("report.json")),
		TEXT("data_assets"));

	TestTrue(TEXT("canonical data asset compare succeeds"), Result.bSuccess);
	TestTrue(TEXT("canonical data asset compare returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		return false;
	}

	TestEqual(TEXT("mode stays data_assets"), Result.Data->GetStringField(TEXT("mode")), TEXT("data_assets"));
	TestEqual(
		TEXT("changed count is one"),
		static_cast<int32>(Result.Data->GetObjectField(TEXT("counts"))->GetNumberField(TEXT("changed"))),
		1);
	return true;
}

bool FCortexDataJsonDiffExplicitWrapperModesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	const FString LeftPath = Fixture.WriteJsonFile(TEXT("left.json"), TEXT(R"({"items":[{"id":"QuestA","Priority":26}]})"));
	const FString RightPath = Fixture.WriteJsonFile(TEXT("right.json"), TEXT(R"({"records":[{"id":"QuestA","Priority":28}]})"));
	const FString ReportPath = Fixture.MakeSavedPath(TEXT("report.json"));

	const FCortexCommandResult Result = Fixture.CompareJson(LeftPath, RightPath, ReportPath, TEXT("datatable_rows"), TEXT("id"));
	TestTrue(TEXT("explicit wrapper compare succeeds"), Result.bSuccess);
	return true;
}

bool FCortexDataJsonDiffExplicitKeyFieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	const FString LeftPath = Fixture.WriteJsonFile(TEXT("left.json"), TEXT(R"([{"id":"QuestA","Priority":26}])"));
	const FString RightPath = Fixture.WriteJsonFile(TEXT("right.json"), TEXT(R"([{"id":"QuestA","Priority":28}])"));
	const FString ReportPath = Fixture.MakeSavedPath(TEXT("report.json"));

	const FCortexCommandResult Result = Fixture.CompareJson(LeftPath, RightPath, ReportPath, TEXT("datatable_rows"), TEXT("id"));
	TestTrue(TEXT("explicit key_field compare succeeds"), Result.bSuccess);
	TestTrue(TEXT("explicit key_field compare returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		return false;
	}

	TestEqual(
		TEXT("changed count is one"),
		static_cast<int32>(Result.Data->GetObjectField(TEXT("counts"))->GetNumberField(TEXT("changed"))),
		1);
	return true;
}

bool FCortexDataJsonDiffDuplicateKeysFailTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataJsonDiffTestFixture Fixture;
	const FString LeftPath = Fixture.WriteJsonFile(TEXT("left.json"), TEXT(R"([{"id":"QuestA","Priority":26},{"id":"QuestA","Priority":27}])"));
	const FString RightPath = Fixture.WriteJsonFile(TEXT("right.json"), TEXT(R"([{"id":"QuestA","Priority":28}])"));
	const FString ReportPath = Fixture.MakeSavedPath(TEXT("report.json"));

	const FCortexCommandResult Result = Fixture.CompareJson(LeftPath, RightPath, ReportPath, TEXT("datatable_rows"), TEXT("id"));
	TestFalse(TEXT("duplicate keys fail"), Result.bSuccess);
	TestEqual(TEXT("duplicate keys use InvalidField"), Result.ErrorCode, CortexErrorCodes::InvalidField);
	return true;
}
