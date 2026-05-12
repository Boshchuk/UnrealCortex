#include "Misc/AutomationTest.h"
#include "Modules/ModuleManager.h"
#include "CortexCoreModule.h"
#include "CortexCommandRouter.h"
#include "CortexStateTreeCommandHandler.h"
#include "ICortexCommandRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeModuleRegistrationTest,
	"Cortex.StateTree.Module.RegistersDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeModuleRegistrationTest::RunTest(const FString& Parameters)
{
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("CortexStateTree"));

	FCortexCoreModule& CoreModule =
		FModuleManager::GetModuleChecked<FCortexCoreModule>(TEXT("CortexCore"));
	FCortexCommandResult Capabilities =
		CoreModule.GetCommandRouter().Execute(TEXT("get_capabilities"), MakeShared<FJsonObject>());

	TestTrue(TEXT("get_capabilities succeeds"), Capabilities.bSuccess);

	const TSharedPtr<FJsonObject>* Domains = nullptr;
	TestTrue(TEXT("capabilities contains domains"),
		Capabilities.Data.IsValid()
		&& Capabilities.Data->TryGetObjectField(TEXT("domains"), Domains)
		&& Domains != nullptr);

	const TSharedPtr<FJsonObject>* StateTreeDomain = nullptr;
	TestTrue(TEXT("capabilities contains statetree domain"),
		Domains != nullptr
		&& (*Domains)->TryGetObjectField(TEXT("statetree"), StateTreeDomain)
		&& StateTreeDomain != nullptr);

	const TArray<TSharedPtr<FJsonValue>>* Commands = nullptr;
	TestTrue(TEXT("statetree publishes command metadata"),
		StateTreeDomain != nullptr
		&& (*StateTreeDomain)->TryGetArrayField(TEXT("commands"), Commands)
		&& Commands != nullptr);

	TSet<FString> CommandNames;
	for (const TSharedPtr<FJsonValue>& CommandValue : *Commands)
	{
		const TSharedPtr<FJsonObject>* CommandObject = nullptr;
		if (CommandValue.IsValid() && CommandValue->TryGetObject(CommandObject) && CommandObject != nullptr)
		{
			FString Name;
			if ((*CommandObject)->TryGetStringField(TEXT("name"), Name))
			{
				CommandNames.Add(Name);
			}
		}
	}

	const TSet<FString> ExpectedNames = {
		TEXT("list_assets"),
		TEXT("create_asset"),
		TEXT("duplicate_asset"),
		TEXT("delete_asset"),
		TEXT("dump_tree"),
		TEXT("get_state"),
		TEXT("check_structure"),
		TEXT("validate_asset"),
		TEXT("compile"),
		TEXT("add_state"),
		TEXT("remove_state"),
		TEXT("rename_state"),
		TEXT("move_state"),
		TEXT("set_state_properties"),
		TEXT("add_transition"),
		TEXT("remove_transition"),
		TEXT("set_transition_properties"),
	};

	TestEqual(TEXT("statetree command set matches v1"), CommandNames.Num(), ExpectedNames.Num());
	for (const FString& ExpectedName : ExpectedNames)
	{
		TestTrue(FString::Printf(TEXT("command exists: %s"), *ExpectedName), CommandNames.Contains(ExpectedName));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeUnknownCommandTest,
	"Cortex.StateTree.Module.UnknownCommand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeUnknownCommandTest::RunTest(const FString& Parameters)
{
	FCortexStateTreeCommandHandler Handler;
	FCortexCommandResult Result = Handler.Execute(TEXT("missing_command"), MakeShared<FJsonObject>());

	TestFalse(TEXT("unknown command fails"), Result.bSuccess);
	TestEqual(TEXT("unknown command error code"), Result.ErrorCode, CortexErrorCodes::UnknownCommand);
	return true;
}
