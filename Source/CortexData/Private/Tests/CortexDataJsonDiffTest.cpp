#include "CoreMinimal.h"
#include "CortexCommandRouter.h"
#include "CortexDataCommandHandler.h"
#include "CortexTypes.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"

namespace
{
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexDataJsonDiffCommandsRegisteredTest,
	"Cortex.Data.JsonDiff.CommandsRegistered",
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

	FCortexCommandRouter Router;
	Router.RegisterDomain(
		TEXT("data"),
		TEXT("Cortex Data"),
		TEXT("1.0.1"),
		MakeShared<FCortexDataCommandHandler>());

	TSharedPtr<FJsonObject> ParamsObject = MakeShared<FJsonObject>();
	ParamsObject->SetStringField(TEXT("left_path"), TEXT("Saved/CortexReports/left.json"));
	ParamsObject->SetStringField(TEXT("right_path"), TEXT("Saved/CortexReports/right.json"));
	ParamsObject->SetStringField(TEXT("report_path"), TEXT("Saved/CortexReports/diff.json"));

	const FCortexCommandResult Result = Router.Execute(TEXT("data.compare_data_json"), ParamsObject);
	TestNotEqual(TEXT("command is routed, not unknown"), Result.ErrorCode, CortexErrorCodes::UnknownCommand);

	return true;
}
