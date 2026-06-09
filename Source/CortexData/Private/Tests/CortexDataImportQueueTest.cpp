#include "CoreMinimal.h"
#include "CortexCommandRouter.h"
#include "CortexDataCommandHandler.h"
#include "CortexSafeFileContract.h"
#include "CortexSerializer.h"
#include "CortexTypes.h"
#include "Operations/CortexDataMutationHelpers.h"
#include "Tests/CortexDataLocalizationTestTypes.h"
#include "Tests/CortexTestDataAsset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectHash.h"

namespace
{
	const TCHAR* ImportQueueTestRoot = TEXT("/Game/CortexImportQueueTests");

	FCortexCommandRouter CreateDataImportQueueTestRouter()
	{
		FCortexCommandRouter Router;
		Router.RegisterDomain(
			TEXT("data"),
			TEXT("Cortex Data"),
			TEXT("1.0.1"),
			MakeShared<FCortexDataCommandHandler>());
		return Router;
	}

	bool ImportQueueSupportedCommandNamesContain(const TArray<FCortexCommandInfo>& Commands, const FString& CommandName)
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

	FCortexDataLocalizationTestRow MakeImportQueueRow(const FString& Title, const FString& StepText)
	{
		FCortexDataLocalizationTestRow Row;
		Row.Title = FText::FromString(Title);
		Row.row_name = FString::Printf(TEXT("Data %s"), *Title);

		FCortexDataLocalizationStepTestRow Step;
		Step.Description = FText::FromString(StepText);
		Row.Steps.Add(Step);

		return Row;
	}

	FString ImportQueuePackagePathForAsset(const FString& RunId, const FString& AssetName)
	{
		return FString::Printf(TEXT("%s/%s/%s"), ImportQueueTestRoot, *RunId, *AssetName);
	}

	void DeleteImportQueuePackageFile(const FString& PackageName)
	{
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PackageName,
			FPackageName::GetAssetPackageExtension());

		if (IFileManager::Get().FileExists(*Filename))
		{
			IFileManager::Get().Delete(*Filename);
		}
	}

	void CleanupImportQueueLoadedPackage(const FString& PackageName)
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

	void CleanupImportQueuePackageByName(const FString& PackageName)
	{
		CleanupImportQueueLoadedPackage(PackageName);
		DeleteImportQueuePackageFile(PackageName);
	}

	class FCortexDataImportQueueTestFixture
	{
	public:
		FCortexDataImportQueueTestFixture()
			: RunId(FString::Printf(TEXT("Run_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
		}

		~FCortexDataImportQueueTestFixture()
		{
			Cleanup();
		}

		UDataTable* CreateRegularDataTable()
		{
			UDataTable* Table = CreateAsset<UDataTable>(TEXT("DT_CortexImportQueueRows"));
			if (Table == nullptr)
			{
				return nullptr;
			}

			Table->RowStruct = FCortexDataLocalizationTestRow::StaticStruct();

			Table->AddRow(TEXT("zeta"), MakeImportQueueRow(TEXT("Zeta"), TEXT("Third inserted")));
			Table->AddRow(TEXT("alpha"), MakeImportQueueRow(TEXT("Alpha"), TEXT("Second inserted")));
			Table->AddRow(TEXT("middle"), MakeImportQueueRow(TEXT("Middle"), TEXT("First inserted")));

			return Table;
		}

		UCortexTestDataAsset* CreateDataAsset()
		{
			return CreateAsset<UCortexTestDataAsset>(TEXT("DA_CortexImportQueueAsset"));
		}

		UStringTable* CreateStringTable()
		{
			UStringTable* Table = CreateAsset<UStringTable>(TEXT("ST_CortexImportQueueStrings"));
			if (Table != nullptr)
			{
				Table->GetMutableStringTable()->SetNamespace(TEXT("CortexImportQueueTests"));
			}
			return Table;
		}

		FCortexCommandRouter CreateRouter() const
		{
			return CreateDataImportQueueTestRouter();
		}

		FString MakeSavedOutputPath(const FString& FileName) const
		{
			return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CortexImportQueueTests"), RunId, FileName);
		}

		void WriteQueueFile(const FString& FilePath, const FString& QueueId, const TArray<TSharedRef<FJsonObject>>& Operations) const
		{
			TSharedRef<FJsonObject> Queue = MakeShared<FJsonObject>();
			Queue->SetNumberField(TEXT("schema_version"), 1);
			Queue->SetStringField(TEXT("queue_id"), QueueId);
			Queue->SetStringField(TEXT("domain"), TEXT("test"));
			Queue->SetBoolField(TEXT("valid"), true);
			Queue->SetStringField(TEXT("generator"), TEXT("CortexDataImportQueueTest"));

			TArray<TSharedPtr<FJsonValue>> OperationValues;
			for (const TSharedRef<FJsonObject>& Operation : Operations)
			{
				OperationValues.Add(MakeShared<FJsonValueObject>(Operation));
			}
			Queue->SetArrayField(TEXT("operations"), OperationValues);

			const FString Contents = FCortexSafeFileContract::SerializeCanonicalJson(Queue);
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
			FFileHelper::SaveStringToFile(Contents, *FilePath);
		}

		TSharedRef<FJsonObject> MakeUpdateRowOperation(
			const FString& Id,
			const FString& TablePath,
			const FString& RowName,
			const TSharedRef<FJsonObject>& RowData) const
		{
			TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>();
			Params->SetStringField(TEXT("table_path"), TablePath);
			Params->SetStringField(TEXT("row_name"), RowName);
			Params->SetObjectField(TEXT("row_data"), RowData);

			TSharedRef<FJsonObject> Operation = MakeShared<FJsonObject>();
			Operation->SetStringField(TEXT("id"), Id);
			Operation->SetStringField(TEXT("phase"), TEXT("apply"));
			Operation->SetStringField(TEXT("command"), TEXT("update_datatable_row"));
			Operation->SetObjectField(TEXT("params"), Params);
			Operation->SetStringField(TEXT("source_page"), TEXT("DesignWiki/Test.md"));
			return Operation;
		}

		bool TryReadJsonFile(const FString& FilePath, TSharedPtr<FJsonObject>& OutJson, FString& OutError) const
		{
			FString Contents;
			if (!FFileHelper::LoadFileToString(Contents, *FilePath))
			{
				OutError = FString::Printf(TEXT("Failed to read JSON file: %s"), *FilePath);
				return false;
			}

			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
			if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
			{
				OutError = FString::Printf(TEXT("Failed to parse JSON file: %s"), *FilePath);
				return false;
			}

			return true;
		}

		void Cleanup()
		{
			for (int32 Index = CreatedPackageNames.Num() - 1; Index >= 0; --Index)
			{
				CleanupImportQueuePackageByName(CreatedPackageNames[Index]);
			}
			CreatedPackageNames.Empty();
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(MakeSavedOutputPath(TEXT("cleanup.json"))), false, true);
		}

	private:
		template <typename AssetType>
		AssetType* CreateAsset(const FString& AssetName)
		{
			const FString PackageName = ImportQueuePackagePathForAsset(RunId, AssetName);
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
	FCortexDataImportQueueCommandsRegisteredTest,
	"Cortex.Data.ImportQueue.CommandsRegistered",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueCommandsRegisteredTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FCortexDataCommandHandler Handler;
	TestTrue(
		TEXT("apply_import_ops_json is advertised"),
		ImportQueueSupportedCommandNamesContain(Handler.GetSupportedCommands(), TEXT("apply_import_ops_json")));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("ops_path"), TEXT("Saved/CortexImports/missing.json"));
	Params->SetStringField(TEXT("report_path"), TEXT("Saved/CortexImports/report.json"));

	FCortexCommandRouter Router = CreateDataImportQueueTestRouter();
	const FCortexCommandResult Result = Router.Execute(TEXT("data.apply_import_ops_json"), Params);

	TestFalse(TEXT("missing ops file fails"), Result.bSuccess);
	TestEqual(TEXT("missing ops file returns FileNotFound"), Result.ErrorCode, CortexErrorCodes::FileNotFound);
	TestNotEqual(TEXT("missing ops file is not UnknownCommand"), Result.ErrorCode, CortexErrorCodes::UnknownCommand);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueDryRunDefaultWritesReportTest,
	"Cortex.Data.ImportQueue.DryRunDefaultWritesReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueDryRunDefaultWritesReportTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UDataTable* Table = Fixture.CreateRegularDataTable();
	TestNotNull(TEXT("DataTable fixture is created"), Table);
	if (Table == nullptr)
	{
		return false;
	}

	const FString OpsPath = Fixture.MakeSavedOutputPath(TEXT("queue.json"));
	const FString ReportPath = Fixture.MakeSavedOutputPath(TEXT("report.json"));
	TSharedRef<FJsonObject> RowData = MakeShared<FJsonObject>();
	RowData->SetStringField(TEXT("row_name"), TEXT("Imported Alpha"));
	Fixture.WriteQueueFile(
		OpsPath,
		TEXT("queue-dry-run"),
		{ Fixture.MakeUpdateRowOperation(TEXT("op.update.row_name"), Table->GetPathName(), TEXT("alpha"), RowData) });

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("ops_path"), OpsPath);
	Params->SetStringField(TEXT("report_path"), ReportPath);

	const FCortexCommandResult Result = Fixture.CreateRouter().Execute(TEXT("data.apply_import_ops_json"), Params);
	TestTrue(TEXT("dry-run queue succeeds"), Result.bSuccess);
	TestTrue(TEXT("dry-run queue returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		if (!Result.ErrorMessage.IsEmpty())
		{
			AddError(Result.ErrorMessage);
		}
		return false;
	}

	TestEqual(TEXT("status is dry_run_ok"), Result.Data->GetStringField(TEXT("status")), TEXT("dry_run_ok"));
	TestTrue(TEXT("dry_run defaults true"), Result.Data->GetBoolField(TEXT("dry_run")));
	TestFalse(TEXT("applied is false"), Result.Data->GetBoolField(TEXT("applied")));
	TestFalse(TEXT("MCP summary omits operations array"), Result.Data->HasField(TEXT("operations")));
	TestTrue(TEXT("report file is written"), IFileManager::Get().FileExists(*ReportPath));

	TSharedPtr<FJsonObject> Report;
	FString ParseError;
	TestTrue(TEXT("report parses"), Fixture.TryReadJsonFile(ReportPath, Report, ParseError));
	if (Report.IsValid())
	{
		TestEqual(TEXT("report queue_id matches"), Report->GetStringField(TEXT("queue_id")), TEXT("queue-dry-run"));
		TestEqual(TEXT("report operation_count"), static_cast<int32>(Report->GetNumberField(TEXT("operation_count"))), 1);
		TestEqual(TEXT("report previewed_count"), static_cast<int32>(Report->GetNumberField(TEXT("previewed_count"))), 1);
		TestTrue(TEXT("report contains operations array"), Report->HasTypedField<EJson::Array>(TEXT("operations")));
		TestTrue(TEXT("report contains ops hash"), Report->HasTypedField<EJson::String>(TEXT("ops_sha256")));
	}

	const FCortexDataLocalizationTestRow* Row =
		reinterpret_cast<const FCortexDataLocalizationTestRow*>(Table->FindRowUnchecked(TEXT("alpha")));
	TestNotNull(TEXT("row still exists after dry run"), Row);
	if (Row != nullptr)
	{
		TestNotEqual(TEXT("dry-run does not mutate row field"), Row->row_name, TEXT("Imported Alpha"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueHelperParityUpdateDatatableRowTest,
	"Cortex.Data.ImportQueue.HelperParity.UpdateDatatableRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueHelperParityUpdateDatatableRowTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UDataTable* DataTable = Fixture.CreateRegularDataTable();
	TestNotNull(TEXT("regular DataTable fixture is created"), DataTable);
	if (DataTable == nullptr)
	{
		return false;
	}

	TSharedPtr<FJsonObject> RowData = MakeShared<FJsonObject>();
	RowData->SetStringField(TEXT("row_name"), TEXT("Imported Alpha"));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("table_path"), DataTable->GetPathName());
	Params->SetStringField(TEXT("row_name"), TEXT("alpha"));
	Params->SetObjectField(TEXT("row_data"), RowData);
	Params->SetBoolField(TEXT("dry_run"), false);

	FCortexCommandRouter Router = CreateDataImportQueueTestRouter();
	const FCortexCommandResult Result = Router.Execute(TEXT("data.update_datatable_row"), Params);

	TestTrue(TEXT("update_datatable_row succeeds"), Result.bSuccess);
	TestTrue(TEXT("update_datatable_row returns data"), Result.Data.IsValid());
	if (!Result.bSuccess || !Result.Data.IsValid())
	{
		if (!Result.ErrorMessage.IsEmpty())
		{
			AddError(Result.ErrorMessage);
		}
		return false;
	}

	TestTrue(TEXT("modified_fields exists"), Result.Data->HasTypedField<EJson::Array>(TEXT("modified_fields")));

	const uint8* RowDataPtr = DataTable->FindRowUnchecked(TEXT("alpha"));
	TestNotNull(TEXT("alpha row still exists"), RowDataPtr);
	if (RowDataPtr == nullptr)
	{
		return false;
	}

	const FCortexDataLocalizationTestRow* AlphaRow = reinterpret_cast<const FCortexDataLocalizationTestRow*>(RowDataPtr);
	TestEqual(TEXT("alpha row field changed through public router"), AlphaRow->row_name, TEXT("Imported Alpha"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueHelperParityUpdateDatatableRowRejectsUnknownFieldTest,
	"Cortex.Data.ImportQueue.HelperParity.UpdateDatatableRowRejectsUnknownField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueHelperParityUpdateDatatableRowRejectsUnknownFieldTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UDataTable* DataTable = Fixture.CreateRegularDataTable();
	TestNotNull(TEXT("regular DataTable fixture is created"), DataTable);
	if (DataTable == nullptr)
	{
		return false;
	}

	const uint8* OriginalRowDataPtr = DataTable->FindRowUnchecked(TEXT("alpha"));
	TestNotNull(TEXT("alpha row exists before update"), OriginalRowDataPtr);
	if (OriginalRowDataPtr == nullptr)
	{
		return false;
	}
	const FCortexDataLocalizationTestRow* OriginalAlphaRow = reinterpret_cast<const FCortexDataLocalizationTestRow*>(OriginalRowDataPtr);
	const FString OriginalRowName = OriginalAlphaRow->row_name;

	TSharedPtr<FJsonObject> RowData = MakeShared<FJsonObject>();
	RowData->SetStringField(TEXT("row_name"), TEXT("Should Not Apply"));
	RowData->SetStringField(TEXT("unknown_field"), TEXT("invalid"));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("table_path"), DataTable->GetPathName());
	Params->SetStringField(TEXT("row_name"), TEXT("alpha"));
	Params->SetObjectField(TEXT("row_data"), RowData);
	Params->SetBoolField(TEXT("dry_run"), false);

	FCortexCommandRouter Router = CreateDataImportQueueTestRouter();
	const FCortexCommandResult Result = Router.Execute(TEXT("data.update_datatable_row"), Params);

	TestFalse(TEXT("update_datatable_row rejects unknown fields during planning"), Result.bSuccess);
	TestEqual(TEXT("unknown field failure uses SerializationError"), Result.ErrorCode, CortexErrorCodes::SerializationError);

	const uint8* CurrentRowDataPtr = DataTable->FindRowUnchecked(TEXT("alpha"));
	TestNotNull(TEXT("alpha row still exists after rejected update"), CurrentRowDataPtr);
	if (CurrentRowDataPtr == nullptr)
	{
		return false;
	}
	const FCortexDataLocalizationTestRow* CurrentAlphaRow = reinterpret_cast<const FCortexDataLocalizationTestRow*>(CurrentRowDataPtr);
	TestEqual(TEXT("rejected update does not mutate existing row field"), CurrentAlphaRow->row_name, OriginalRowName);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueHelperParityUpdateDataAssetRejectsUnknownPropertyTest,
	"Cortex.Data.ImportQueue.HelperParity.UpdateDataAssetRejectsUnknownProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueHelperParityUpdateDataAssetRejectsUnknownPropertyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UCortexTestDataAsset* DataAsset = Fixture.CreateDataAsset();
	TestNotNull(TEXT("DataAsset fixture is created"), DataAsset);
	if (DataAsset == nullptr)
	{
		return false;
	}

	DataAsset->TestProperty = TEXT("Original Value");
	DataAsset->TestNumber = 7;

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("TestProperty"), TEXT("Should Not Apply"));
	Properties->SetStringField(TEXT("UnknownProperty"), TEXT("invalid"));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("asset_path"), DataAsset->GetPathName());
	Params->SetObjectField(TEXT("properties"), Properties);
	Params->SetBoolField(TEXT("dry_run"), false);

	FCortexCommandRouter Router = CreateDataImportQueueTestRouter();
	const FCortexCommandResult Result = Router.Execute(TEXT("data.update_data_asset"), Params);

	TestFalse(TEXT("update_data_asset rejects unknown properties during planning"), Result.bSuccess);
	TestEqual(TEXT("unknown property failure uses SerializationError"), Result.ErrorCode, CortexErrorCodes::SerializationError);
	TestEqual(TEXT("rejected update does not mutate existing string property"), DataAsset->TestProperty, TEXT("Original Value"));
	TestEqual(TEXT("rejected update does not mutate existing numeric property"), DataAsset->TestNumber, 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueHelperParityUpdateStringTableRejectsMalformedOperationTest,
	"Cortex.Data.ImportQueue.HelperParity.UpdateStringTableRejectsMalformedOperation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueHelperParityUpdateStringTableRejectsMalformedOperationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UStringTable* StringTable = Fixture.CreateStringTable();
	TestNotNull(TEXT("StringTable fixture is created"), StringTable);
	if (StringTable == nullptr)
	{
		return false;
	}

	StringTable->GetMutableStringTable()->SetSourceString(TEXT("existing"), TEXT("Original"));

	TSharedRef<FJsonObject> ValidSetOperation = MakeShared<FJsonObject>();
	ValidSetOperation->SetStringField(TEXT("type"), TEXT("set"));
	ValidSetOperation->SetStringField(TEXT("key"), TEXT("new_key"));
	ValidSetOperation->SetStringField(TEXT("source_string"), TEXT("Should Not Apply"));

	TSharedRef<FJsonObject> MalformedSetOperation = MakeShared<FJsonObject>();
	MalformedSetOperation->SetStringField(TEXT("type"), TEXT("set"));
	MalformedSetOperation->SetStringField(TEXT("source_string"), TEXT("Missing key"));

	TArray<TSharedPtr<FJsonValue>> Operations;
	Operations.Add(MakeShared<FJsonValueObject>(ValidSetOperation));
	Operations.Add(MakeShared<FJsonValueObject>(MalformedSetOperation));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("string_table_path"), StringTable->GetPathName());
	Params->SetArrayField(TEXT("operations"), Operations);
	Params->SetBoolField(TEXT("dry_run"), false);
	Params->SetBoolField(TEXT("allow_partial"), false);

	FCortexCommandRouter Router = CreateDataImportQueueTestRouter();
	const FCortexCommandResult Result = Router.Execute(TEXT("data.update_string_table"), Params);

	TestFalse(TEXT("update_string_table rejects malformed operations during planning"), Result.bSuccess);
	TestEqual(TEXT("malformed operation failure uses InvalidField"), Result.ErrorCode, CortexErrorCodes::InvalidField);

	FString ExistingValue;
	TestTrue(TEXT("existing key remains available"), StringTable->GetStringTable()->GetSourceString(TEXT("existing"), ExistingValue));
	TestEqual(TEXT("existing key remains unchanged"), ExistingValue, TEXT("Original"));

	FString NewValue;
	TestFalse(TEXT("valid operation before malformed operation is not applied"), StringTable->GetStringTable()->GetSourceString(TEXT("new_key"), NewValue));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueHelperParityUpdateDataAssetNoOpMetadataTest,
	"Cortex.Data.ImportQueue.HelperParity.UpdateDataAssetNoOpMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueHelperParityUpdateDataAssetNoOpMetadataTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UCortexTestDataAsset* DataAsset = Fixture.CreateDataAsset();
	TestNotNull(TEXT("DataAsset fixture is created"), DataAsset);
	if (DataAsset == nullptr)
	{
		return false;
	}

	DataAsset->TestProperty = TEXT("Same Value");

	FCortexUpdateDataAssetMutationRequest Request;
	Request.AssetPath = DataAsset->GetPathName();
	Request.Properties = MakeShared<FJsonObject>();
	Request.Properties->SetStringField(TEXT("TestProperty"), TEXT("Same Value"));
	Request.bDryRun = false;

	FCortexUpdateDataAssetMutationPlan Plan;
	FCortexDataMutationResult BuildResult = FCortexDataMutationHelpers::BuildUpdateDataAssetPlan(Request, Plan);
	TestTrue(TEXT("no-op DataAsset update plan builds"), BuildResult.bSuccess);
	if (!BuildResult.bSuccess)
	{
		if (BuildResult.Errors.Num() > 0)
		{
			AddError(BuildResult.Errors[0].Message);
		}
		return false;
	}

	FCortexDataMutationResult ApplyResult = FCortexDataMutationHelpers::ApplyUpdateDataAsset(Plan);
	TestTrue(TEXT("no-op DataAsset update applies successfully"), ApplyResult.bSuccess);
	TestFalse(TEXT("no-op DataAsset apply does not report changed"), ApplyResult.bChanged);
	TestTrue(TEXT("no-op DataAsset apply reports no-op"), ApplyResult.bNoOp);
	TestEqual(TEXT("no-op DataAsset apply keeps property unchanged"), DataAsset->TestProperty, TEXT("Same Value"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataImportQueueHelperParityImportDatatableJsonNoOpMetadataTest,
	"Cortex.Data.ImportQueue.HelperParity.ImportDatatableJsonNoOpMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataImportQueueHelperParityImportDatatableJsonNoOpMetadataTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexDataImportQueueTestFixture Fixture;
	UDataTable* DataTable = Fixture.CreateRegularDataTable();
	TestNotNull(TEXT("regular DataTable fixture is created"), DataTable);
	if (DataTable == nullptr)
	{
		return false;
	}

	const UScriptStruct* RowStruct = DataTable->GetRowStruct();
	const uint8* ExistingRow = DataTable->FindRowUnchecked(TEXT("alpha"));
	TestNotNull(TEXT("alpha row exists before import"), ExistingRow);
	if (RowStruct == nullptr || ExistingRow == nullptr)
	{
		return false;
	}

	TSharedPtr<FJsonObject> RowData = FCortexSerializer::StructToJson(RowStruct, ExistingRow);
	TestTrue(TEXT("existing row serializes for import no-op test"), RowData.IsValid());
	if (!RowData.IsValid())
	{
		return false;
	}

	TSharedRef<FJsonObject> Row = MakeShared<FJsonObject>();
	Row->SetStringField(TEXT("row_name"), TEXT("alpha"));
	Row->SetObjectField(TEXT("row_data"), RowData);

	FCortexImportDatatableJsonMutationRequest Request;
	Request.TablePath = DataTable->GetPathName();
	Request.Rows.Add(MakeShared<FJsonValueObject>(Row));
	Request.Mode = TEXT("upsert");
	Request.bDryRun = false;

	FCortexImportDatatableJsonMutationPlan Plan;
	FCortexDataMutationResult BuildResult = FCortexDataMutationHelpers::BuildImportDatatableJsonPlan(Request, Plan);
	TestTrue(TEXT("identical upsert DataTable import plan builds"), BuildResult.bSuccess);
	if (!BuildResult.bSuccess)
	{
		if (BuildResult.Errors.Num() > 0)
		{
			AddError(BuildResult.Errors[0].Message);
		}
		return false;
	}

	FCortexDataMutationResult ApplyResult = FCortexDataMutationHelpers::ApplyImportDatatableJson(Plan);
	TestTrue(TEXT("identical upsert DataTable import applies successfully"), ApplyResult.bSuccess);
	TestFalse(TEXT("identical upsert DataTable import does not report changed"), ApplyResult.bChanged);
	TestTrue(TEXT("identical upsert DataTable import reports no-op"), ApplyResult.bNoOp);

	return true;
}
