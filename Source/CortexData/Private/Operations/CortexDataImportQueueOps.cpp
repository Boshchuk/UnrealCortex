#include "Operations/CortexDataImportQueueOps.h"

#include "CortexSafeFileContract.h"
#include "CortexTypes.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Operations/CortexDataMutationHelpers.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr int32 SupportedSchemaVersion = 1;

	const TCHAR* StatusDryRunOk = TEXT("dry_run_ok");
	const TCHAR* StatusPreflightFailed = TEXT("preflight_failed");

	const TSet<FString> SupportedCommands = {
		TEXT("update_datatable_row"),
		TEXT("import_datatable_json"),
		TEXT("update_string_table"),
		TEXT("set_translation"),
		TEXT("update_data_asset")
	};

	struct FImportQueueFlags
	{
		bool bDryRun = true;
		bool bApply = false;
		bool bStopOnError = true;
		bool bQueryBack = true;
		bool bAllowPartial = false;
	};

	struct FImportQueueOperation
	{
		int32 Index = INDEX_NONE;
		FString Id;
		FString Phase;
		FString Command;
		TSharedPtr<FJsonObject> Params;
		FString SourcePage;
	};

	struct FImportQueueDocument
	{
		int32 SchemaVersion = 0;
		FString QueueId;
		FString Domain;
		FString Generator;
		TArray<FImportQueueOperation> Operations;
	};

	struct FImportQueueCounts
	{
		int32 OperationCount = 0;
		int32 ValidatedCount = 0;
		int32 PreviewedCount = 0;
		int32 AttemptedCount = 0;
		int32 AppliedCount = 0;
		int32 ChangedCount = 0;
		int32 NoOpCount = 0;
		int32 FailedCount = 0;
		int32 SkippedCount = 0;
		int32 WarningCount = 0;
		int32 ErrorCount = 0;
		int32 SaveRequestedCount = 0;
		int32 SavedCount = 0;
		int32 SaveFailedCount = 0;
	};

	TSharedPtr<FJsonObject> MakeFieldDetails(const FString& FieldName)
	{
		TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
		Details->SetStringField(TEXT("field"), FieldName);
		return Details;
	}

	FCortexCommandResult MakeInvalidFieldError(const FString& Message, const FString& FieldName)
	{
		return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, Message, MakeFieldDetails(FieldName));
	}

	void SetCountFields(TSharedRef<FJsonObject> Object, const FImportQueueCounts& Counts)
	{
		Object->SetNumberField(TEXT("operation_count"), Counts.OperationCount);
		Object->SetNumberField(TEXT("validated_count"), Counts.ValidatedCount);
		Object->SetNumberField(TEXT("previewed_count"), Counts.PreviewedCount);
		Object->SetNumberField(TEXT("attempted_count"), Counts.AttemptedCount);
		Object->SetNumberField(TEXT("applied_count"), Counts.AppliedCount);
		Object->SetNumberField(TEXT("changed_count"), Counts.ChangedCount);
		Object->SetNumberField(TEXT("no_op_count"), Counts.NoOpCount);
		Object->SetNumberField(TEXT("failed_count"), Counts.FailedCount);
		Object->SetNumberField(TEXT("skipped_count"), Counts.SkippedCount);
		Object->SetNumberField(TEXT("warning_count"), Counts.WarningCount);
		Object->SetNumberField(TEXT("error_count"), Counts.ErrorCount);
		Object->SetNumberField(TEXT("save_requested_count"), Counts.SaveRequestedCount);
		Object->SetNumberField(TEXT("saved_count"), Counts.SavedCount);
		Object->SetNumberField(TEXT("save_failed_count"), Counts.SaveFailedCount);
	}

	bool RejectUnknownTopLevelParams(
		const TSharedPtr<FJsonObject>& Params,
		const TSet<FString>& AllowedFields,
		FCortexCommandResult& OutError)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params->Values)
		{
			if (!AllowedFields.Contains(Pair.Key))
			{
				OutError = MakeInvalidFieldError(
					FString::Printf(TEXT("Unknown top-level param: %s"), *Pair.Key),
					Pair.Key);
				return false;
			}
		}

		return true;
	}

	bool ParseQueueCommandParams(
		const TSharedPtr<FJsonObject>& Params,
		FString& OutOpsPath,
		FString& OutReportPath,
		FImportQueueFlags& OutFlags,
		FCortexCommandResult& OutError)
	{
		if (!Params.IsValid())
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidField,
				TEXT("Missing required params: ops_path and report_path"));
			return false;
		}

		const TSet<FString> AllowedFields = {
			TEXT("ops_path"),
			TEXT("report_path"),
			TEXT("dry_run"),
			TEXT("apply"),
			TEXT("stop_on_error"),
			TEXT("query_back"),
			TEXT("allow_partial"),
		};

		if (!RejectUnknownTopLevelParams(Params, AllowedFields, OutError))
		{
			return false;
		}

		if (!Params->TryGetStringField(TEXT("ops_path"), OutOpsPath)
			|| !Params->TryGetStringField(TEXT("report_path"), OutReportPath))
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidField,
				TEXT("Missing required params: ops_path and report_path"));
			return false;
		}

		Params->TryGetBoolField(TEXT("dry_run"), OutFlags.bDryRun);
		Params->TryGetBoolField(TEXT("apply"), OutFlags.bApply);
		Params->TryGetBoolField(TEXT("stop_on_error"), OutFlags.bStopOnError);
		Params->TryGetBoolField(TEXT("query_back"), OutFlags.bQueryBack);
		Params->TryGetBoolField(TEXT("allow_partial"), OutFlags.bAllowPartial);

		if (OutFlags.bDryRun && OutFlags.bApply)
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				TEXT("dry_run=true with apply=true is contradictory"));
			return false;
		}

		if (!OutFlags.bDryRun && !OutFlags.bApply)
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				TEXT("Real apply requires dry_run=false and apply=true"));
			return false;
		}

		return true;
	}

	bool ParseQueueDocument(
		const FString& Contents,
		FImportQueueDocument& OutQueue,
		FCortexCommandResult& OutError)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Contents);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::MalformedJson,
				TEXT("Operation queue file is not valid JSON"));
			return false;
		}

		double SchemaVersion = 0.0;
		if (!Root->TryGetNumberField(TEXT("schema_version"), SchemaVersion)
			|| static_cast<int32>(SchemaVersion) != SupportedSchemaVersion)
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidQueueShape,
				TEXT("Operation queue schema_version must be 1"));
			return false;
		}
		OutQueue.SchemaVersion = static_cast<int32>(SchemaVersion);

		if (!Root->TryGetStringField(TEXT("queue_id"), OutQueue.QueueId) || OutQueue.QueueId.IsEmpty())
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidQueueShape,
				TEXT("Operation queue requires non-empty queue_id"));
			return false;
		}

		bool bValid = false;
		if (!Root->TryGetBoolField(TEXT("valid"), bValid) || !bValid)
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidQueueShape,
				TEXT("Operation queue requires valid=true"));
			return false;
		}

		Root->TryGetStringField(TEXT("domain"), OutQueue.Domain);
		Root->TryGetStringField(TEXT("generator"), OutQueue.Generator);

		const TArray<TSharedPtr<FJsonValue>>* OperationValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("operations"), OperationValues) || OperationValues == nullptr)
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidQueueShape,
				TEXT("Operation queue requires operations array"));
			return false;
		}

		TSet<FString> SeenIds;
		for (int32 Index = 0; Index < OperationValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& OperationValue = (*OperationValues)[Index];
			if (!OperationValue.IsValid() || OperationValue->Type != EJson::Object)
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::InvalidQueueShape,
					FString::Printf(TEXT("Operation %d must be an object"), Index));
				return false;
			}

			const TSharedPtr<FJsonObject>& OperationObject = OperationValue->AsObject();
			if (!OperationObject.IsValid())
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::InvalidQueueShape,
					FString::Printf(TEXT("Operation %d must be an object"), Index));
				return false;
			}

			FImportQueueOperation Operation;
			Operation.Index = Index;
			OperationObject->TryGetStringField(TEXT("id"), Operation.Id);
			OperationObject->TryGetStringField(TEXT("phase"), Operation.Phase);
			OperationObject->TryGetStringField(TEXT("command"), Operation.Command);
			OperationObject->TryGetStringField(TEXT("source_page"), Operation.SourcePage);

			if (Operation.Id.IsEmpty())
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::InvalidQueueShape,
					FString::Printf(TEXT("Operation %d requires non-empty id"), Index));
				return false;
			}

			if (SeenIds.Contains(Operation.Id))
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::InvalidQueueShape,
					FString::Printf(TEXT("Duplicate operation id: %s"), *Operation.Id));
				return false;
			}
			SeenIds.Add(Operation.Id);

			if (Operation.Phase != TEXT("apply"))
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::InvalidQueueShape,
					FString::Printf(TEXT("Operation %s phase must be \"apply\""), *Operation.Id));
				return false;
			}

			if (!SupportedCommands.Contains(Operation.Command))
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::UnsupportedCommand,
					FString::Printf(TEXT("Unsupported import operation command: %s"), *Operation.Command));
				return false;
			}

			const TSharedPtr<FJsonObject>* ParamsObject = nullptr;
			if (!OperationObject->TryGetObjectField(TEXT("params"), ParamsObject)
				|| ParamsObject == nullptr
				|| !ParamsObject->IsValid())
			{
				OutError = FCortexCommandRouter::Error(
					CortexErrorCodes::InvalidOperation,
					FString::Printf(TEXT("Operation %s requires params object"), *Operation.Id));
				return false;
			}

			Operation.Params = *ParamsObject;
			OutQueue.Operations.Add(Operation);
		}

		return true;
	}

	void GetAllowedOperationFields(
		const FString& Command,
		TSet<FString>& OutRequired,
		TSet<FString>& OutOptional)
	{
		OutRequired.Empty();
		OutOptional.Empty();

		if (Command == TEXT("update_datatable_row"))
		{
			OutRequired = { TEXT("table_path"), TEXT("row_name"), TEXT("row_data") };
			OutOptional = { TEXT("dry_run") };
			return;
		}

		if (Command == TEXT("import_datatable_json"))
		{
			OutRequired = { TEXT("table_path"), TEXT("rows") };
			OutOptional = { TEXT("mode"), TEXT("dry_run") };
			return;
		}

		if (Command == TEXT("update_string_table"))
		{
			OutRequired = { TEXT("string_table_path"), TEXT("operations") };
			OutOptional = { TEXT("save"), TEXT("verbose"), TEXT("dry_run") };
			return;
		}

		if (Command == TEXT("set_translation"))
		{
			OutRequired = { TEXT("string_table_path"), TEXT("key"), TEXT("text") };
			OutOptional = { TEXT("save"), TEXT("dry_run") };
			return;
		}

		if (Command == TEXT("update_data_asset"))
		{
			OutRequired = { TEXT("asset_path"), TEXT("properties") };
			OutOptional = { TEXT("dry_run") };
		}
	}

	bool RejectUnknownOrControlledParams(
		const FImportQueueOperation& Operation,
		const FImportQueueFlags& Flags,
		FCortexCommandResult& OutError)
	{
		if (!Operation.Params.IsValid())
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				FString::Printf(TEXT("Operation %s requires params object"), *Operation.Id));
			return false;
		}

		if (Operation.Params->HasField(TEXT("apply")))
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				TEXT("Operation-level apply is not accepted"));
			return false;
		}

		if (Operation.Params->HasField(TEXT("allow_partial")))
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				TEXT("Operation-level allow_partial is not accepted"));
			return false;
		}

		bool bOperationDryRun = false;
		if (Operation.Params->TryGetBoolField(TEXT("dry_run"), bOperationDryRun)
			&& bOperationDryRun != Flags.bDryRun)
		{
			OutError = FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				TEXT("Operation-level dry_run conflicts with queue mode"));
			return false;
		}

		TSet<FString> RequiredFields;
		TSet<FString> OptionalFields;
		GetAllowedOperationFields(Operation.Command, RequiredFields, OptionalFields);

		for (const FString& RequiredField : RequiredFields)
		{
			if (!Operation.Params->HasField(RequiredField))
			{
				OutError = MakeInvalidFieldError(
					FString::Printf(TEXT("Operation %s missing required param: %s"), *Operation.Id, *RequiredField),
					RequiredField);
				return false;
			}
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Operation.Params->Values)
		{
			if (!RequiredFields.Contains(Pair.Key) && !OptionalFields.Contains(Pair.Key))
			{
				OutError = MakeInvalidFieldError(
					FString::Printf(TEXT("Operation %s has unknown param: %s"), *Operation.Id, *Pair.Key),
					Pair.Key);
				return false;
			}
		}

		return true;
	}

	TSharedRef<FJsonObject> CloneOperationParamsWithQueueFlags(
		const FImportQueueOperation& Operation,
		const FImportQueueFlags& Flags)
	{
		TSharedRef<FJsonObject> Params = MakeShared<FJsonObject>(*Operation.Params);
		Params->SetBoolField(TEXT("dry_run"), Flags.bDryRun);
		if (Operation.Command == TEXT("update_string_table"))
		{
			Params->SetBoolField(TEXT("allow_partial"), Flags.bAllowPartial);
		}
		return Params;
	}

	FCortexDataMutationResult ExecuteQueueOperationPreview(
		const FImportQueueOperation& Operation,
		const FImportQueueFlags& Flags)
	{
		FCortexCommandResult ValidationError;
		if (!RejectUnknownOrControlledParams(Operation, Flags, ValidationError))
		{
			return FCortexDataMutationResult::FromCommandResult(ValidationError);
		}

		const TSharedRef<FJsonObject> Params = CloneOperationParamsWithQueueFlags(Operation, Flags);

		if (Operation.Command == TEXT("update_datatable_row"))
		{
			FCortexUpdateDatatableRowMutationRequest Request;
			FCortexDataMutationResult ParseResult = FCortexDataMutationHelpers::ParseUpdateDatatableRowParams(Params, Request);
			if (!ParseResult.bSuccess)
			{
				return ParseResult;
			}

			FCortexUpdateDatatableRowMutationPlan Plan;
			FCortexDataMutationResult PlanResult = FCortexDataMutationHelpers::BuildUpdateDatatableRowPlan(Request, Plan);
			if (!PlanResult.bSuccess)
			{
				return PlanResult;
			}

			return FCortexDataMutationHelpers::PreviewUpdateDatatableRow(Plan);
		}

		if (Operation.Command == TEXT("import_datatable_json"))
		{
			FCortexImportDatatableJsonMutationRequest Request;
			FCortexDataMutationResult ParseResult = FCortexDataMutationHelpers::ParseImportDatatableJsonParams(Params, Request);
			if (!ParseResult.bSuccess)
			{
				return ParseResult;
			}

			FCortexImportDatatableJsonMutationPlan Plan;
			FCortexDataMutationResult PlanResult = FCortexDataMutationHelpers::BuildImportDatatableJsonPlan(Request, Plan);
			if (!PlanResult.bSuccess)
			{
				return PlanResult;
			}

			return FCortexDataMutationHelpers::PreviewImportDatatableJson(Plan);
		}

		if (Operation.Command == TEXT("update_string_table"))
		{
			FCortexUpdateStringTableMutationRequest Request;
			FCortexDataMutationResult ParseResult = FCortexDataMutationHelpers::ParseUpdateStringTableParams(Params, Request);
			if (!ParseResult.bSuccess)
			{
				return ParseResult;
			}

			FCortexUpdateStringTableMutationPlan Plan;
			FCortexDataMutationResult PlanResult = FCortexDataMutationHelpers::BuildUpdateStringTablePlan(Request, Plan);
			if (!PlanResult.bSuccess)
			{
				return PlanResult;
			}

			return FCortexDataMutationHelpers::PreviewUpdateStringTable(Plan);
		}

		if (Operation.Command == TEXT("set_translation"))
		{
			FCortexSetTranslationMutationRequest Request;
			FCortexDataMutationResult ParseResult = FCortexDataMutationHelpers::ParseSetTranslationParams(Params, Request);
			if (!ParseResult.bSuccess)
			{
				return ParseResult;
			}

			FCortexSetTranslationMutationPlan Plan;
			FCortexDataMutationResult PlanResult = FCortexDataMutationHelpers::BuildSetTranslationPlan(Request, Plan);
			if (!PlanResult.bSuccess)
			{
				return PlanResult;
			}

			Plan.UpdatePlan.Request.bDryRun = true;
			return FCortexDataMutationHelpers::PreviewUpdateStringTable(Plan.UpdatePlan);
		}

		if (Operation.Command == TEXT("update_data_asset"))
		{
			FCortexUpdateDataAssetMutationRequest Request;
			FCortexDataMutationResult ParseResult = FCortexDataMutationHelpers::ParseUpdateDataAssetParams(Params, Request);
			if (!ParseResult.bSuccess)
			{
				return ParseResult;
			}

			FCortexUpdateDataAssetMutationPlan Plan;
			FCortexDataMutationResult PlanResult = FCortexDataMutationHelpers::BuildUpdateDataAssetPlan(Request, Plan);
			if (!PlanResult.bSuccess)
			{
				return PlanResult;
			}

			return FCortexDataMutationHelpers::PreviewUpdateDataAsset(Plan);
		}

		return FCortexDataMutationResult::FromCommandResult(
			FCortexCommandRouter::Error(
				CortexErrorCodes::UnsupportedCommand,
				FString::Printf(TEXT("Unsupported import operation command: %s"), *Operation.Command)));
	}

	TSharedPtr<FJsonObject> MakeOperationReportObject(
		const FImportQueueOperation& Operation,
		const FCortexDataMutationResult& Result)
	{
		TSharedPtr<FJsonObject> OperationObject = MakeShared<FJsonObject>();
		OperationObject->SetNumberField(TEXT("index"), Operation.Index);
		OperationObject->SetStringField(TEXT("id"), Operation.Id);
		OperationObject->SetStringField(TEXT("phase"), Operation.Phase);
		OperationObject->SetStringField(TEXT("command"), Operation.Command);
		OperationObject->SetStringField(TEXT("status"), Result.bSuccess ? TEXT("dry_run") : TEXT("failed"));

		if (!Operation.SourcePage.IsEmpty())
		{
			OperationObject->SetStringField(TEXT("source_page"), Operation.SourcePage);
		}

		if (!Result.Target.IsEmpty())
		{
			OperationObject->SetStringField(TEXT("target"), Result.Target);
		}

		if (Result.PublicData.IsValid())
		{
			OperationObject->SetObjectField(TEXT("result"), Result.PublicData);
		}

		if (Result.Warnings.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> WarningValues;
			for (const FString& Warning : Result.Warnings)
			{
				WarningValues.Add(MakeShared<FJsonValueString>(Warning));
			}
			OperationObject->SetArrayField(TEXT("warnings"), WarningValues);
		}

		if (Result.Errors.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> ErrorValues;
			for (const FCortexDataMutationError& Error : Result.Errors)
			{
				TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
				ErrorObject->SetStringField(TEXT("error_code"), Error.ErrorCode);
				ErrorObject->SetStringField(TEXT("message"), Error.Message);
				if (Error.Details.IsValid())
				{
					ErrorObject->SetObjectField(TEXT("details"), Error.Details);
				}
				ErrorValues.Add(MakeShared<FJsonValueObject>(ErrorObject));
			}
			OperationObject->SetArrayField(TEXT("errors"), ErrorValues);
		}

		return OperationObject;
	}

	TSharedRef<FJsonObject> BuildReport(
		const FImportQueueDocument& Queue,
		const FImportQueueFlags& Flags,
		const FImportQueueCounts& Counts,
		const FString& RequestedReportPath,
		const FString& CanonicalReportPath,
		const FString& OpsHash,
		const TArray<TSharedPtr<FJsonValue>>& OperationValues)
	{
		TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
		Report->SetNumberField(TEXT("schema_version"), Queue.SchemaVersion);
		Report->SetStringField(TEXT("queue_id"), Queue.QueueId);
		Report->SetStringField(TEXT("status"), Counts.FailedCount == 0 ? StatusDryRunOk : StatusPreflightFailed);
		Report->SetBoolField(TEXT("success"), Counts.FailedCount == 0);
		Report->SetBoolField(TEXT("partial"), false);
		Report->SetBoolField(TEXT("dry_run"), Flags.bDryRun);
		Report->SetBoolField(TEXT("applied"), false);
		Report->SetStringField(TEXT("report_path"), RequestedReportPath);
		Report->SetStringField(TEXT("canonical_report_path"), CanonicalReportPath);
		Report->SetStringField(TEXT("ops_sha256"), OpsHash);
		if (!Queue.Domain.IsEmpty())
		{
			Report->SetStringField(TEXT("domain"), Queue.Domain);
		}
		if (!Queue.Generator.IsEmpty())
		{
			Report->SetStringField(TEXT("generator"), Queue.Generator);
		}
		SetCountFields(Report, Counts);
		Report->SetArrayField(TEXT("operations"), OperationValues);
		return Report;
	}

	TSharedRef<FJsonObject> BuildCompactSummary(
		const FImportQueueCounts& Counts,
		const FString& RequestedReportPath,
		const FString& CanonicalReportPath)
	{
		TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
		Summary->SetStringField(TEXT("status"), StatusDryRunOk);
		Summary->SetBoolField(TEXT("success"), true);
		Summary->SetBoolField(TEXT("partial"), false);
		Summary->SetBoolField(TEXT("dry_run"), true);
		Summary->SetBoolField(TEXT("applied"), false);
		Summary->SetStringField(TEXT("report_path"), RequestedReportPath);
		Summary->SetStringField(TEXT("canonical_report_path"), CanonicalReportPath);
		SetCountFields(Summary, Counts);
		return Summary;
	}
}

FCortexCommandResult FCortexDataImportQueueOps::ApplyImportOpsJson(const TSharedPtr<FJsonObject>& Params)
{
	check(IsInGameThread());

	FString OpsPath;
	FString ReportPath;
	FImportQueueFlags Flags;
	FCortexCommandResult ParamError;
	if (!ParseQueueCommandParams(Params, OpsPath, ReportPath, Flags, ParamError))
	{
		return ParamError;
	}

	FCortexResolvedFilePath ResolvedOpsPath;
	FString ErrorCode;
	FString ErrorMessage;
	if (!FCortexSafeFileContract::ResolveReadPath(OpsPath, ResolvedOpsPath, ErrorCode, ErrorMessage))
	{
		return FCortexCommandRouter::Error(ErrorCode, ErrorMessage);
	}

	FCortexResolvedFilePath ResolvedReportPath;
	if (!FCortexSafeFileContract::ResolveWritePath(ReportPath, ResolvedReportPath, ErrorCode, ErrorMessage))
	{
		return FCortexCommandRouter::Error(ErrorCode, ErrorMessage);
	}

	if (FCortexSafeFileContract::AreSameCanonicalFile(
		ResolvedOpsPath.AbsolutePath,
		ResolvedReportPath.AbsolutePath))
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidFilePath,
			TEXT("ops_path and report_path must resolve to different files"));
	}

	if (!FCortexSafeFileContract::PrepareWritePath(ResolvedReportPath, ErrorCode, ErrorMessage))
	{
		return FCortexCommandRouter::Error(ErrorCode, ErrorMessage);
	}

	FString QueueContents;
	if (!FCortexSafeFileContract::ReadTextFile(ResolvedOpsPath, QueueContents, ErrorCode, ErrorMessage))
	{
		return FCortexCommandRouter::Error(ErrorCode, ErrorMessage);
	}

	FString OpsHash;
	if (!FCortexSafeFileContract::HashFileBytesSha256(ResolvedOpsPath, OpsHash, ErrorCode, ErrorMessage))
	{
		return FCortexCommandRouter::Error(ErrorCode, ErrorMessage);
	}

	FImportQueueDocument Queue;
	FCortexCommandResult QueueError;
	if (!ParseQueueDocument(QueueContents, Queue, QueueError))
	{
		return QueueError;
	}

	FImportQueueCounts Counts;
	Counts.OperationCount = Queue.Operations.Num();

	TArray<TSharedPtr<FJsonValue>> OperationValues;
	for (const FImportQueueOperation& Operation : Queue.Operations)
	{
		FCortexDataMutationResult PreviewResult = ExecuteQueueOperationPreview(Operation, Flags);
		if (PreviewResult.bSuccess)
		{
			++Counts.ValidatedCount;
			++Counts.PreviewedCount;
			if (PreviewResult.bChanged)
			{
				++Counts.ChangedCount;
			}
			if (PreviewResult.bNoOp)
			{
				++Counts.NoOpCount;
			}
		}
		else
		{
			++Counts.FailedCount;
			Counts.ErrorCount += PreviewResult.Errors.Num() > 0 ? PreviewResult.Errors.Num() : 1;
		}

		Counts.WarningCount += PreviewResult.Warnings.Num();
		OperationValues.Add(MakeShared<FJsonValueObject>(MakeOperationReportObject(Operation, PreviewResult)));
	}

	TSharedRef<FJsonObject> Report = BuildReport(
		Queue,
		Flags,
		Counts,
		ReportPath,
		ResolvedReportPath.AbsolutePath,
		OpsHash,
		OperationValues);

	FCortexJsonFileWriteResult WriteResult = FCortexSafeFileContract::WriteJsonReportAtomic(ResolvedReportPath, Report);
	if (!WriteResult.bWritten)
	{
		return FCortexCommandRouter::Error(WriteResult.ErrorCode, WriteResult.ErrorMessage);
	}

	if (Counts.FailedCount > 0)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidOperation,
			TEXT("One or more import queue operations failed dry-run validation"),
			Report);
	}

	return FCortexCommandRouter::Success(BuildCompactSummary(Counts, ReportPath, ResolvedReportPath.AbsolutePath));
}
