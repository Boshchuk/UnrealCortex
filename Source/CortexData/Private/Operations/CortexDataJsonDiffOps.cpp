#include "Operations/CortexDataJsonDiffOps.h"

#include "CortexTypes.h"

FCortexCommandResult FCortexDataJsonDiffOps::CompareDataJson(const TSharedPtr<FJsonObject>& Params)
{
	FString LeftPath;
	FString RightPath;
	FString ReportPath;
	if (!Params.IsValid()
		|| !Params->TryGetStringField(TEXT("left_path"), LeftPath)
		|| !Params->TryGetStringField(TEXT("right_path"), RightPath)
		|| !Params->TryGetStringField(TEXT("report_path"), ReportPath))
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required params: left_path, right_path, and report_path"));
	}

	return FCortexCommandRouter::Error(
		CortexErrorCodes::InvalidOperation,
		TEXT("compare_data_json is not implemented yet"));
}
