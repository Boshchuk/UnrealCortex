#include "Operations/CortexEditorUtilityOps.h"
#include "CortexEditorPIEState.h"
#include "CortexEditorLogCapture.h"
#include "CortexCommandRouter.h"
#include "CortexDeferredExec.h"
#include "Misc/App.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "HAL/IConsoleManager.h"
#include "IPythonScriptPlugin.h"

namespace
{
	/** Write a console variable's typed value + type tag into a JSON object. */
	void WriteCVarValue(IConsoleVariable* CVar, const TSharedPtr<FJsonObject>& Data)
	{
		Data->SetStringField(TEXT("value"), CVar->GetString());
		if (CVar->IsVariableInt())
		{
			Data->SetNumberField(TEXT("int_value"), CVar->GetInt());
			Data->SetStringField(TEXT("type"), TEXT("int"));
		}
		else if (CVar->IsVariableFloat())
		{
			Data->SetNumberField(TEXT("float_value"), CVar->GetFloat());
			Data->SetStringField(TEXT("type"), TEXT("float"));
		}
		else if (CVar->IsVariableBool())
		{
			Data->SetBoolField(TEXT("bool_value"), CVar->GetBool());
			Data->SetStringField(TEXT("type"), TEXT("bool"));
		}
		else
		{
			Data->SetStringField(TEXT("type"), TEXT("string"));
		}
	}
}

FCortexCommandResult FCortexEditorUtilityOps::GetEditorState(const FCortexEditorPIEState& PIEState)
{
	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("project_name"), FApp::GetProjectName());
	Data->SetStringField(TEXT("pie_state"), FCortexEditorPIEState::StateToString(PIEState.GetState()));

	FString CurrentMap;
	if (GEditor != nullptr)
	{
		const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		if (EditorWorld != nullptr)
		{
			CurrentMap = EditorWorld->GetMapName();
		}
	}
	Data->SetStringField(TEXT("current_map"), CurrentMap);

	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::GetRecentLogs(
	const FCortexEditorLogCapture& LogCapture,
	const TSharedPtr<FJsonObject>& Params)
{
	FString SeverityStr = TEXT("log");
	double SinceSeconds = 30.0;
	int32 SinceCursor = -1;
	FString Category;

	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("severity"), SeverityStr);
		Params->TryGetNumberField(TEXT("since_seconds"), SinceSeconds);
		Params->TryGetNumberField(TEXT("since_cursor"), SinceCursor);
		Params->TryGetStringField(TEXT("category"), Category);
	}

	ELogVerbosity::Type Severity = ELogVerbosity::Log;
	if (SeverityStr == TEXT("warning"))
	{
		Severity = ELogVerbosity::Warning;
	}
	else if (SeverityStr == TEXT("error"))
	{
		Severity = ELogVerbosity::Error;
	}

	const FCortexEditorLogResult Logs = LogCapture.GetRecentLogs(Severity, SinceSeconds, SinceCursor, Category);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> EntriesArray;
	for (const FCortexEditorLogEntry& Entry : Logs.Entries)
	{
		TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("cursor"), Entry.Cursor);
		Item->SetNumberField(TEXT("timestamp"), Entry.Timestamp);
		Item->SetStringField(TEXT("category"), Entry.Category);
		Item->SetStringField(TEXT("message"), Entry.Message);
		Item->SetStringField(TEXT("severity"),
			Entry.Verbosity == ELogVerbosity::Error ? TEXT("error") :
			Entry.Verbosity == ELogVerbosity::Warning ? TEXT("warning") :
			TEXT("log"));
		EntriesArray.Add(MakeShared<FJsonValueObject>(Item));
	}
	Data->SetArrayField(TEXT("entries"), EntriesArray);
	Data->SetArrayField(TEXT("logs"), EntriesArray);
	Data->SetNumberField(TEXT("count"), EntriesArray.Num());
	Data->SetNumberField(TEXT("cursor"), Logs.Cursor);

	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::ExecuteConsoleCommand(
	const FCortexEditorPIEState& PIEState,
	const TSharedPtr<FJsonObject>& Params)
{
	if (!PIEState.IsActive() || GEditor == nullptr || GEditor->PlayWorld == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::PIENotActive,
			TEXT("PIE is not running. Call start_pie first."));
	}

	FString Command;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required param: command"));
	}

	const bool bOk = GEditor->PlayWorld->Exec(GEditor->PlayWorld, *Command);
	if (!bOk)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::ConsoleCommandFailed,
			FString::Printf(TEXT("Console command failed: %s"), *Command));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("command"), Command);
	Data->SetStringField(TEXT("status"), TEXT("ok"));
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::SetTimeDilation(
	const FCortexEditorPIEState& PIEState,
	const TSharedPtr<FJsonObject>& Params)
{
	double Factor = 1.0;
	if (!Params.IsValid() || !Params->TryGetNumberField(TEXT("factor"), Factor))
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required param: factor"));
	}
	if (Factor < 0.01 || Factor > 20.0)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidValue,
			TEXT("factor must be in range [0.01, 20.0]"));
	}
	if (!PIEState.IsActive() || GEditor == nullptr || GEditor->PlayWorld == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::PIENotActive,
			TEXT("PIE is not running. Call start_pie first."));
	}

	GEditor->PlayWorld->GetWorldSettings()->SetTimeDilation(static_cast<float>(Factor));

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetNumberField(TEXT("time_dilation"), Factor);
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::GetWorldInfo(const FCortexEditorPIEState& PIEState)
{
	if (!PIEState.IsActive() || GEditor == nullptr || GEditor->PlayWorld == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::PIENotActive,
			TEXT("PIE is not running. Call start_pie first."));
	}

	UWorld* PIEWorld = GEditor->PlayWorld;
	AWorldSettings* WS = PIEWorld->GetWorldSettings();

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("map_name"), PIEWorld->GetMapName());
	Data->SetNumberField(TEXT("time_seconds"), PIEWorld->GetTimeSeconds());
	Data->SetNumberField(TEXT("time_dilation"), WS ? WS->GetEffectiveTimeDilation() : 1.0);
	Data->SetNumberField(TEXT("gravity_z"), WS ? WS->GetGravityZ() : 0.0);
	Data->SetNumberField(TEXT("kill_z"), WS ? WS->KillZ : 0.0);
	if (WS && WS->DefaultGameMode)
	{
		Data->SetStringField(TEXT("game_mode"), WS->DefaultGameMode->GetPathName());
	}
	else
	{
		Data->SetStringField(TEXT("game_mode"), TEXT(""));
	}

	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::GetCVar(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required param: name"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (CVar == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Console variable not found: %s"), *Name));
	}

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	WriteCVarValue(CVar, Data);
	Data->SetStringField(TEXT("help"), CVar->GetHelp());
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::SetCVar(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required param: name"));
	}

	// TryGetStringField coerces JSON numbers/bools to string, so callers may pass any of them.
	FString Value;
	if (!Params->TryGetStringField(TEXT("value"), Value))
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required param: value"));
	}

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Name);
	if (CVar == nullptr)
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::SymbolNotFound,
			FString::Printf(TEXT("Console variable not found: %s"), *Name));
	}

	const FString OldValue = CVar->GetString();
	CVar->Set(*Value, ECVF_SetByConsole);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("name"), Name);
	Data->SetStringField(TEXT("old_value"), OldValue);
	WriteCVarValue(CVar, Data);
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::ListCVars(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern;
	double LimitIn = 100.0;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("pattern"), Pattern);
		Params->TryGetNumberField(TEXT("limit"), LimitIn);
	}
	const int32 MaxItems = FMath::Clamp(static_cast<int32>(LimitIn), 1, 500);

	TArray<TSharedPtr<FJsonValue>> Items;
	int32 Count = 0;
	// FConsoleObjectVisitor is a delegate (DECLARE_DELEGATE_TwoParams), not a TFunctionRef,
	// so a raw lambda does not implicitly convert — wrap it with CreateLambda.
	IConsoleManager::Get().ForEachConsoleObjectThatContains(
		FConsoleObjectVisitor::CreateLambda(
			[&Items, &Count, MaxItems](const TCHAR* ObjName, IConsoleObject* Obj)
			{
				if (Count >= MaxItems || Obj == nullptr)
				{
					return;
				}
				TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), ObjName);
				IConsoleVariable* CVar = Obj->AsVariable();
				if (CVar != nullptr)
				{
					Item->SetBoolField(TEXT("is_variable"), true);
					Item->SetStringField(TEXT("value"), CVar->GetString());
				}
				else
				{
					Item->SetBoolField(TEXT("is_variable"), false);
				}
				Item->SetStringField(TEXT("help"), Obj->GetHelp());
				Items.Add(MakeShared<FJsonValueObject>(Item));
				++Count;
			}),
		*Pattern);

	TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
	Data->SetStringField(TEXT("pattern"), Pattern);
	Data->SetArrayField(TEXT("cvars"), Items);
	Data->SetNumberField(TEXT("count"), Items.Num());
	Data->SetBoolField(TEXT("truncated"), Count >= MaxItems);
	return FCortexCommandRouter::Success(Data);
}

FCortexCommandResult FCortexEditorUtilityOps::RunPython(
	const TSharedPtr<FJsonObject>& Params,
	FDeferredResponseCallback DeferredCallback)
{
	FString Code;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("code"), Code) || Code.IsEmpty())
	{
		return FCortexCommandRouter::Error(
			CortexErrorCodes::InvalidField,
			TEXT("Missing required param: code"));
	}

	bool bDefer = false;
	if (Params.IsValid())
	{
		Params->TryGetBoolField(TEXT("defer"), bDefer);
	}

	auto DoRun = [Code]() -> FCortexCommandResult
	{
		IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
		if (Python == nullptr || !Python->IsPythonAvailable())
		{
			return FCortexCommandRouter::Error(
				CortexErrorCodes::UnsupportedCommand,
				TEXT("Python scripting is not available. Enable the PythonScriptPlugin."));
		}

		FPythonCommandEx Cmd;
		Cmd.Command = Code;
		Cmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;
		Cmd.Flags |= EPythonCommandFlags::Unattended;

		const bool bOk = Python->ExecPythonCommandEx(Cmd);

		TArray<TSharedPtr<FJsonValue>> OutputArr;
		for (const FPythonLogOutputEntry& Entry : Cmd.LogOutput)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			const TCHAR* TypeStr =
				Entry.Type == EPythonLogOutputType::Error ? TEXT("error") :
				Entry.Type == EPythonLogOutputType::Warning ? TEXT("warning") : TEXT("info");
			Item->SetStringField(TEXT("type"), TypeStr);
			Item->SetStringField(TEXT("text"), Entry.Output);
			OutputArr.Add(MakeShared<FJsonValueObject>(Item));
		}

		if (!bOk)
		{
			TSharedPtr<FJsonObject> Details = MakeShared<FJsonObject>();
			Details->SetArrayField(TEXT("output"), OutputArr);
			Details->SetStringField(TEXT("result"), Cmd.CommandResult);
			return FCortexCommandRouter::Error(
				CortexErrorCodes::InvalidOperation,
				TEXT("Python execution reported an error. See details.output."),
				Details);
		}

		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("ok"), true);
		Data->SetStringField(TEXT("result"), Cmd.CommandResult);
		Data->SetArrayField(TEXT("output"), OutputArr);
		return FCortexCommandRouter::Success(Data);
	};

	if (bDefer)
	{
		return FCortexDeferredExec::RunNextTick(MoveTemp(DoRun), MoveTemp(DeferredCallback));
	}
	return DoRun();
}
