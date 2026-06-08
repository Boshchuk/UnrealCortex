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
	FCortexDataExportUnknownCommandTest,
	"Cortex.Data.Export.Datatable.UnknownCommandBeforeRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexDataExportUnknownCommandTest::RunTest(const FString& Parameters)
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

	FCortexCommandRouter Router = CreateDataExportTestRouter();
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("table_path"), RegularTable != nullptr ? RegularTable->GetPathName() : TEXT("/Game/CortexExportTests/Missing.DT_CortexExportRows"));
	Params->SetStringField(TEXT("out_path"), Fixture.MakeSavedOutputPath(TEXT("datatable.json")));

	const FCortexCommandResult Result = Router.Execute(TEXT("data.export_datatable_json"), Params);
	TestFalse(TEXT("export_datatable_json is not registered yet"), Result.bSuccess);
	TestEqual(TEXT("unknown command before registration"), Result.ErrorCode, CortexErrorCodes::UnknownCommand);

	return true;
}
