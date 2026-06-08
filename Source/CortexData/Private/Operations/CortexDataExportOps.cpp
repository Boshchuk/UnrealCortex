#include "Operations/CortexDataExportOps.h"

#include "CortexFileUtils.h"
#include "CortexSerializer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "Internationalization/StringTable.h"
#include "UObject/UnrealType.h"

namespace
{
	bool IsWindowsDeviceOrUncPath(const FString& Path)
	{
		return Path.StartsWith(TEXT("\\\\?\\"))
			|| Path.StartsWith(TEXT("\\\\.\\"))
			|| (Path.StartsWith(TEXT("\\\\")) && !Path.StartsWith(TEXT("\\\\?\\")) && !Path.StartsWith(TEXT("\\\\.\\")));
	}

	bool ContainsTraversalSegment(const FString& Path)
	{
		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("/"), true);
		for (const FString& Segment : Segments)
		{
			if (Segment == TEXT(".."))
			{
				return true;
			}
		}

		return false;
	}

	bool IsDriveRelativePath(const FString& Path)
	{
		return Path.Len() >= 2
			&& FChar::IsAlpha(Path[0])
			&& Path[1] == TEXT(':')
			&& (Path.Len() == 2 || (Path[2] != TEXT('/') && Path[2] != TEXT('\\')));
	}

	FString NormalizeForComparison(const FString& InPath)
	{
		FString Path = InPath;
		FPaths::NormalizeFilename(Path);
		FPaths::CollapseRelativeDirectories(Path);
		FPaths::RemoveDuplicateSlashes(Path);
		FPaths::NormalizeDirectoryName(Path);
		return Path;
	}

	bool IsUnderDirectory(const FString& Candidate, const FString& Root)
	{
		const FString NormalizedCandidate = NormalizeForComparison(Candidate);
		const FString NormalizedRoot = NormalizeForComparison(Root);

		if (NormalizedCandidate.Equals(NormalizedRoot, ESearchCase::IgnoreCase))
		{
			return true;
		}

		const FString RootWithSeparator = NormalizedRoot.EndsWith(TEXT("/"))
			? NormalizedRoot
			: NormalizedRoot + TEXT("/");

		return NormalizedCandidate.StartsWith(RootWithSeparator, ESearchCase::IgnoreCase);
	}

	FString ResolveExistingParentPath(const FString& ParentPath)
	{
		FString Current = ParentPath;
		FPaths::NormalizeFilename(Current);
		FPaths::CollapseRelativeDirectories(Current);

		while (!Current.IsEmpty())
		{
			if (IFileManager::Get().DirectoryExists(*Current))
			{
				return FPaths::ConvertRelativePathToFull(IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*Current));
			}

			const FString Next = FPaths::GetPath(Current);
			if (Next == Current)
			{
				break;
			}
			Current = Next;
		}

		return TEXT("");
	}

	bool ContainsSymlinkOrJunctionSegment(const FString& ParentPath)
	{
		FString Current = ParentPath;
		FPaths::NormalizeFilename(Current);
		FPaths::CollapseRelativeDirectories(Current);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		while (!Current.IsEmpty())
		{
			if (IFileManager::Get().DirectoryExists(*Current)
				&& PlatformFile.IsSymlink(*Current) == ESymlinkResult::Symlink)
			{
				return true;
			}

			const FString Next = FPaths::GetPath(Current);
			if (Next == Current)
			{
				break;
			}
			Current = Next;
		}

		return false;
	}

	FCortexCommandResult InvalidFieldError(const FString& Message)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, Message);
	}
}

bool FCortexDataExportOps::TryResolveOutputPath(const FString& InPath, FResolvedOutputPath& OutPath, FString& OutError)
{
	FString TrimmedPath = InPath;
	TrimmedPath.TrimStartAndEndInline();

	if (TrimmedPath.IsEmpty())
	{
		OutError = TEXT("Output path cannot be empty");
		return false;
	}

	if (TrimmedPath.EndsWith(TEXT("/")) || TrimmedPath.EndsWith(TEXT("\\")))
	{
		OutError = FString::Printf(TEXT("Output path must include a file name: %s"), *InPath);
		return false;
	}

	FString SlashPath = TrimmedPath;
	FPaths::NormalizeFilename(SlashPath);

	if (IsWindowsDeviceOrUncPath(TrimmedPath) || IsWindowsDeviceOrUncPath(SlashPath))
	{
		OutError = FString::Printf(TEXT("Output path is not allowed: %s"), *InPath);
		return false;
	}

	if (IsDriveRelativePath(SlashPath))
	{
		OutError = FString::Printf(TEXT("Drive-relative output path is not allowed: %s"), *InPath);
		return false;
	}

	if (ContainsTraversalSegment(SlashPath))
	{
		OutError = FString::Printf(TEXT("Output path cannot contain traversal segments: %s"), *InPath);
		return false;
	}

	if (FPaths::GetCleanFilename(SlashPath).IsEmpty())
	{
		OutError = FString::Printf(TEXT("Output path must include a file name: %s"), *InPath);
		return false;
	}

	const FString ProjectRoot = NormalizeForComparison(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
	const FString ProjectSaved = NormalizeForComparison(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()));

	FString Candidate = SlashPath;
	if (FPaths::IsRelative(Candidate))
	{
		Candidate = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir(), Candidate);
	}
	else
	{
		Candidate = FPaths::ConvertRelativePathToFull(Candidate);
	}

	FPaths::NormalizeFilename(Candidate);
	if (!FPaths::CollapseRelativeDirectories(Candidate))
	{
		OutError = FString::Printf(TEXT("Output path could not be normalized: %s"), *InPath);
		return false;
	}
	FPaths::RemoveDuplicateSlashes(Candidate);

	if (IFileManager::Get().DirectoryExists(*Candidate))
	{
		OutError = FString::Printf(TEXT("Output path is an existing directory: %s"), *InPath);
		return false;
	}

	if (!IsUnderDirectory(Candidate, ProjectRoot) && !IsUnderDirectory(Candidate, ProjectSaved))
	{
		OutError = FString::Printf(TEXT("Output path must be under the project root or Saved directory: %s"), *InPath);
		return false;
	}

	const FString ParentPath = FPaths::GetPath(Candidate);
	if (ParentPath.IsEmpty() || ParentPath == Candidate)
	{
		OutError = FString::Printf(TEXT("Output path must include a parent directory and file name: %s"), *InPath);
		return false;
	}

	const FString ExistingParentPath = ResolveExistingParentPath(ParentPath);
	if (!ExistingParentPath.IsEmpty() && ContainsSymlinkOrJunctionSegment(ParentPath))
	{
		OutError = FString::Printf(TEXT("Output path parent contains a symlink or junction: %s"), *InPath);
		return false;
	}

	if (!ExistingParentPath.IsEmpty()
		&& !IsUnderDirectory(ExistingParentPath, ProjectRoot)
		&& !IsUnderDirectory(ExistingParentPath, ProjectSaved))
	{
		OutError = FString::Printf(TEXT("Output path parent resolves outside allowed roots: %s"), *InPath);
		return false;
	}

	OutPath.AbsolutePath = NormalizeForComparison(Candidate);
	return true;
}

bool FCortexDataExportOps::TryResolveBulkItemPath(const FString& OutDir, const FString& ItemOutPath, FResolvedOutputPath& OutPath, FString& OutError)
{
	if (ItemOutPath.IsEmpty())
	{
		OutError = TEXT("Bulk item output path cannot be empty");
		return false;
	}

	FString ItemPath = ItemOutPath;
	FPaths::NormalizeFilename(ItemPath);
	if (!FPaths::IsRelative(ItemPath))
	{
		OutError = FString::Printf(TEXT("Bulk item output path must be relative under out_dir: %s"), *ItemOutPath);
		return false;
	}

	return TryResolveOutputPath(FPaths::Combine(OutDir, ItemPath), OutPath, OutError);
}

FCortexDataExportOps::FExportWriteResult FCortexDataExportOps::WriteJsonFile(const FString& AbsolutePath, const TSharedRef<FJsonObject>& Payload)
{
	FExportWriteResult Result;
	const FString Contents = SerializeCanonicalJson(Payload);
	if (!FCortexFileUtils::AtomicWriteFile(AbsolutePath, Contents))
	{
		Result.Error = FString::Printf(TEXT("Failed to write JSON file: %s"), *AbsolutePath);
		return Result;
	}

	Result.bWritten = true;
	FTCHARToUTF8 Utf8Contents(*Contents);
	Result.BytesWritten = Utf8Contents.Length();
	return Result;
}

FString FCortexDataExportOps::SerializeCanonicalJson(const TSharedRef<FJsonObject>& Payload)
{
	FString Output;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	WriteCanonicalObject(Payload, *Writer);
	Writer->Close();
	return Output;
}

void FCortexDataExportOps::WriteCanonicalValue(const TSharedPtr<FJsonValue>& Value, TJsonWriter<>& Writer)
{
	if (!Value.IsValid() || Value->Type == EJson::Null)
	{
		Writer.WriteNull();
		return;
	}

	switch (Value->Type)
	{
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (Value->TryGetObject(Object) && Object != nullptr)
		{
			WriteCanonicalObject(*Object, Writer);
		}
		else
		{
			Writer.WriteNull();
		}
		break;
	}
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value->TryGetArray(Array) && Array != nullptr)
		{
			Writer.WriteArrayStart();
			for (const TSharedPtr<FJsonValue>& Entry : *Array)
			{
				WriteCanonicalValue(Entry, Writer);
			}
			Writer.WriteArrayEnd();
		}
		else
		{
			Writer.WriteNull();
		}
		break;
	}
	case EJson::String:
	{
		FString StringValue;
		Value->TryGetString(StringValue);
		Writer.WriteValue(StringValue);
		break;
	}
	case EJson::Number:
		Writer.WriteValue(Value->AsNumber());
		break;
	case EJson::Boolean:
		Writer.WriteValue(Value->AsBool());
		break;
	default:
		Writer.WriteNull();
		break;
	}
}

void FCortexDataExportOps::WriteCanonicalObject(const TSharedPtr<FJsonObject>& Object, TJsonWriter<>& Writer)
{
	Writer.WriteObjectStart();
	if (Object.IsValid())
	{
		TArray<FString> Keys;
		Object->Values.GetKeys(Keys);
		Keys.Sort();

		for (const FString& Key : Keys)
		{
			const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Key);
			if (Value == nullptr)
			{
				continue;
			}

			Writer.WriteIdentifierPrefix(Key);
			WriteCanonicalValue(*Value, Writer);
		}
	}
	Writer.WriteObjectEnd();
}

TSet<FString> FCortexDataExportOps::ParseStringSetParam(const TSharedPtr<FJsonObject>& Params, const FString& FieldName)
{
	TSet<FString> Values;
	if (!Params.IsValid())
	{
		return Values;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Params->TryGetArrayField(FieldName, Array) || Array == nullptr)
	{
		return Values;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		FString StringValue;
		if (Value.IsValid() && Value->TryGetString(StringValue))
		{
			Values.Add(StringValue);
		}
	}

	return Values;
}

TArray<FString> FCortexDataExportOps::ParseStringArrayParam(const TSharedPtr<FJsonObject>& Params, const FString& FieldName)
{
	TArray<FString> Values;
	if (!Params.IsValid())
	{
		return Values;
	}

	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (!Params->TryGetArrayField(FieldName, Array) || Array == nullptr)
	{
		return Values;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		FString StringValue;
		if (Value.IsValid() && Value->TryGetString(StringValue))
		{
			Values.Add(StringValue);
		}
	}

	return Values;
}

TSharedRef<FJsonObject> FCortexDataExportOps::MakeSingleSummary(
	bool bCompleted,
	bool bPartial,
	const FString& OutPath,
	int64 BytesWritten,
	int32 ExportedCount,
	const TArray<FString>& Warnings,
	const TArray<FString>& Errors)
{
	TSharedRef<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("completed"), bCompleted);
	Data->SetBoolField(TEXT("partial"), bPartial);
	Data->SetStringField(TEXT("out_path"), OutPath);
	Data->SetNumberField(TEXT("bytes_written"), static_cast<double>(BytesWritten));
	Data->SetNumberField(TEXT("exported_count"), ExportedCount);

	TArray<TSharedPtr<FJsonValue>> WarningValues;
	for (const FString& Warning : Warnings)
	{
		WarningValues.Add(MakeShared<FJsonValueString>(Warning));
	}
	Data->SetArrayField(TEXT("warnings"), WarningValues);

	TArray<TSharedPtr<FJsonValue>> ErrorValues;
	for (const FString& Error : Errors)
	{
		ErrorValues.Add(MakeShared<FJsonValueString>(Error));
	}
	Data->SetArrayField(TEXT("errors"), ErrorValues);
	return Data;
}

UDataTable* FCortexDataExportOps::LoadDataTableForExport(const FString& TablePath, FCortexCommandResult& OutError)
{
	const FString PkgName = FPackageName::ObjectPathToPackageName(TablePath);
	if (!FindPackage(nullptr, *PkgName) && !FPackageName::DoesPackageExist(PkgName))
	{
		OutError = FCortexCommandRouter::Error(
			CortexErrorCodes::TableNotFound,
			FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
		return nullptr;
	}

	UDataTable* DataTable = LoadObject<UDataTable>(nullptr, *TablePath);
	if (DataTable == nullptr)
	{
		OutError = FCortexCommandRouter::Error(
			CortexErrorCodes::TableNotFound,
			FString::Printf(TEXT("DataTable not found: %s"), *TablePath));
	}
	return DataTable;
}

UStringTable* FCortexDataExportOps::LoadStringTableForExport(const FString& TablePath, FCortexCommandResult& OutError)
{
	const FString PkgName = FPackageName::ObjectPathToPackageName(TablePath);
	if (!FindPackage(nullptr, *PkgName) && !FPackageName::DoesPackageExist(PkgName))
	{
		OutError = FCortexCommandRouter::Error(
			CortexErrorCodes::AssetNotFound,
			FString::Printf(TEXT("StringTable not found: %s"), *TablePath));
		return nullptr;
	}

	UStringTable* StringTable = LoadObject<UStringTable>(nullptr, *TablePath);
	if (StringTable == nullptr)
	{
		OutError = FCortexCommandRouter::Error(
			CortexErrorCodes::AssetNotFound,
			FString::Printf(TEXT("StringTable not found: %s"), *TablePath));
	}
	return StringTable;
}

UClass* FCortexDataExportOps::ResolveDataAssetExportClass(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	UClass* Class = FindObject<UClass>(nullptr, *ClassName);
	if (Class != nullptr && Class->IsChildOf(UDataAsset::StaticClass()))
	{
		return Class;
	}

	return nullptr;
}

bool FCortexDataExportOps::ShouldExportDataAssetProperty(const FProperty* Property)
{
	if (Property == nullptr)
	{
		return false;
	}

	const EPropertyFlags BlockedFlags =
		CPF_Transient |
		CPF_DuplicateTransient |
		CPF_NonPIEDuplicateTransient |
		CPF_Deprecated |
		CPF_EditorOnly;

	if (Property->HasAnyPropertyFlags(BlockedFlags))
	{
		return false;
	}

	return Property->HasAnyPropertyFlags(CPF_Edit);
}

TSharedPtr<FJsonObject> FCortexDataExportOps::ExportEditableProperties(const UDataAsset* DataAsset)
{
	(void)DataAsset;
	return MakeShared<FJsonObject>();
}

FCortexCommandResult FCortexDataExportOps::ExportDatatableJson(const TSharedPtr<FJsonObject>& Params)
{
	FString TablePath;
	FString OutPath;
	if (!Params.IsValid()
		|| !Params->TryGetStringField(TEXT("table_path"), TablePath)
		|| !Params->TryGetStringField(TEXT("out_path"), OutPath))
	{
		return InvalidFieldError(TEXT("Missing required params: table_path and out_path"));
	}

	FResolvedOutputPath ResolvedOutPath;
	FString PathError;
	if (!TryResolveOutputPath(OutPath, ResolvedOutPath, PathError))
	{
		return InvalidFieldError(PathError);
	}

	FCortexCommandResult LoadError;
	UDataTable* DataTable = LoadDataTableForExport(TablePath, LoadError);
	if (DataTable == nullptr)
	{
		return LoadError;
	}

	TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("z_marker"), TEXT("data_export_skeleton"));

	TArray<TSharedPtr<FJsonValue>> ArrayOrder;
	ArrayOrder.Add(MakeShared<FJsonValueString>(TEXT("first")));
	ArrayOrder.Add(MakeShared<FJsonValueString>(TEXT("second")));
	Payload->SetArrayField(TEXT("array_order"), ArrayOrder);

	TSharedRef<FJsonObject> NestedOrder = MakeShared<FJsonObject>();
	NestedOrder->SetStringField(TEXT("beta"), TEXT("second"));
	NestedOrder->SetStringField(TEXT("alpha"), TEXT("first"));

	TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
	Summary->SetStringField(TEXT("status"), TEXT("skeleton"));
	Summary->SetObjectField(TEXT("nested_order"), NestedOrder);
	Payload->SetObjectField(TEXT("summary"), Summary);

	const FExportWriteResult WriteResult = WriteJsonFile(ResolvedOutPath.AbsolutePath, Payload);
	if (!WriteResult.bWritten)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::SaveFailed, WriteResult.Error);
	}

	return FCortexCommandRouter::Success(MakeSingleSummary(
		false,
		false,
		ResolvedOutPath.AbsolutePath,
		WriteResult.BytesWritten,
		0,
		TArray<FString>(),
		TArray<FString>{ TEXT("DataTable export payload is not implemented yet") }));
}

FCortexCommandResult FCortexDataExportOps::ExportStringTableJson(const TSharedPtr<FJsonObject>& Params)
{
	FString StringTablePath;
	FString OutPath;
	if (!Params.IsValid()
		|| !Params->TryGetStringField(TEXT("string_table_path"), StringTablePath)
		|| !Params->TryGetStringField(TEXT("out_path"), OutPath))
	{
		return InvalidFieldError(TEXT("Missing required params: string_table_path and out_path"));
	}

	FResolvedOutputPath ResolvedOutPath;
	FString PathError;
	if (!TryResolveOutputPath(OutPath, ResolvedOutPath, PathError))
	{
		return InvalidFieldError(PathError);
	}

	FCortexCommandResult LoadError;
	UStringTable* StringTable = LoadStringTableForExport(StringTablePath, LoadError);
	if (StringTable == nullptr)
	{
		return LoadError;
	}

	return FCortexCommandRouter::Success(MakeSingleSummary(
		false,
		false,
		ResolvedOutPath.AbsolutePath,
		0,
		0,
		TArray<FString>(),
		TArray<FString>{ TEXT("StringTable export payload is not implemented yet") }));
}

FCortexCommandResult FCortexDataExportOps::ExportDataAssetsJson(const TSharedPtr<FJsonObject>& Params)
{
	FString OutPath;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("out_path"), OutPath))
	{
		return InvalidFieldError(TEXT("Missing required param: out_path"));
	}

	FResolvedOutputPath ResolvedOutPath;
	FString PathError;
	if (!TryResolveOutputPath(OutPath, ResolvedOutPath, PathError))
	{
		return InvalidFieldError(PathError);
	}

	FString ClassName;
	if (!Params->TryGetStringField(TEXT("class_name"), ClassName))
	{
		Params->TryGetStringField(TEXT("class_filter"), ClassName);
	}

	FString PathFilter;
	Params->TryGetStringField(TEXT("path_filter"), PathFilter);
	ParseStringArrayParam(Params, TEXT("asset_paths"));

	bool bIncludeProperties = false;
	bool bAllowPartial = false;
	Params->TryGetBoolField(TEXT("include_properties"), bIncludeProperties);
	Params->TryGetBoolField(TEXT("allow_partial"), bAllowPartial);
	(void)ClassName;
	(void)PathFilter;
	(void)bIncludeProperties;
	(void)bAllowPartial;

	return FCortexCommandRouter::Success(MakeSingleSummary(
		false,
		false,
		ResolvedOutPath.AbsolutePath,
		0,
		0,
		TArray<FString>(),
		TArray<FString>{ TEXT("DataAsset export payload is not implemented yet") }));
}

FCortexCommandResult FCortexDataExportOps::ExportBulkJson(const TSharedPtr<FJsonObject>& Params)
{
	FString OutDir;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("out_dir"), OutDir))
	{
		return InvalidFieldError(TEXT("Missing required param: out_dir"));
	}

	if (!Params->TryGetArrayField(TEXT("items"), Items) || Items == nullptr)
	{
		return InvalidFieldError(TEXT("Missing required param: items"));
	}

	FResolvedOutputPath ProbePath;
	FString PathError;
	if (!TryResolveOutputPath(FPaths::Combine(OutDir, TEXT("__cortex_export_probe__.json")), ProbePath, PathError))
	{
		return InvalidFieldError(PathError);
	}

	for (int32 ItemIndex = 0; ItemIndex < Items->Num(); ++ItemIndex)
	{
		const TSharedPtr<FJsonValue>& ItemValue = (*Items)[ItemIndex];
		const TSharedPtr<FJsonObject>* ItemObject = nullptr;
		if (!ItemValue.IsValid() || !ItemValue->TryGetObject(ItemObject) || ItemObject == nullptr || !ItemObject->IsValid())
		{
			return InvalidFieldError(FString::Printf(TEXT("Bulk item at index %d must be an object"), ItemIndex));
		}

		FString ItemOutPath;
		if ((*ItemObject)->TryGetStringField(TEXT("out_path"), ItemOutPath))
		{
			FResolvedOutputPath ResolvedItemPath;
			FString ItemPathError;
			if (!TryResolveBulkItemPath(OutDir, ItemOutPath, ResolvedItemPath, ItemPathError))
			{
				return InvalidFieldError(ItemPathError);
			}
		}
	}

	bool bAllowPartial = false;
	Params->TryGetBoolField(TEXT("allow_partial"), bAllowPartial);
	(void)bAllowPartial;

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetBoolField(TEXT("completed"), false);
	Data->SetBoolField(TEXT("partial"), false);
	Data->SetStringField(TEXT("out_dir"), FPaths::GetPath(ProbePath.AbsolutePath));
	Data->SetNumberField(TEXT("item_count"), Items->Num());
	Data->SetArrayField(TEXT("items"), TArray<TSharedPtr<FJsonValue>>());
	Data->SetStringField(TEXT("status"), TEXT("bulk export orchestration is not implemented yet"));
	return FCortexCommandRouter::Success(Data);
}
