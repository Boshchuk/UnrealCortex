#include "Misc/AutomationTest.h"
#include "CortexEditorCommandHandler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectKeyNoPIETest,
	"Cortex.Editor.Input.InjectKey.ErrorWhenNoPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectKeyNoPIETest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("key"), TEXT("W"));
	Params->SetStringField(TEXT("action"), TEXT("tap"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_key"), Params);

	TestFalse(TEXT("inject_key should fail without PIE"), Result.bSuccess);
	TestEqual(TEXT("Error should be PIE_NOT_ACTIVE"), Result.ErrorCode, TEXT("PIE_NOT_ACTIVE"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputSequenceNoPIETest,
	"Cortex.Editor.Input.InjectInputSequence.ErrorWhenNoPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputSequenceNoPIETest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSharedPtr<FJsonObject> StepObj = MakeShared<FJsonObject>();
	StepObj->SetNumberField(TEXT("at_ms"), 0.0);
	StepObj->SetStringField(TEXT("kind"), TEXT("key"));
	StepObj->SetStringField(TEXT("key"), TEXT("W"));
	StepObj->SetStringField(TEXT("action"), TEXT("tap"));
	Steps.Add(MakeShared<FJsonValueObject>(StepObj));
	Params->SetArrayField(TEXT("steps"), Steps);

	FDeferredResponseCallback Callback = [](FCortexCommandResult) {};
	const FCortexCommandResult Result = Handler.Execute(
		TEXT("inject_input_sequence"), Params, MoveTemp(Callback));

	TestFalse(TEXT("inject_input_sequence should fail without PIE"), Result.bSuccess);
	TestEqual(TEXT("Error should be PIE_NOT_ACTIVE"), Result.ErrorCode, TEXT("PIE_NOT_ACTIVE"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectKeyMissingKeyTest,
	"Cortex.Editor.Input.InjectKey.ErrorWhenMissingKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectKeyMissingKeyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_key"), Params);

	TestFalse(TEXT("inject_key should fail without key param"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectKeyInvalidKeyNameTest,
	"Cortex.Editor.Input.InjectKey.ErrorWhenInvalidKeyName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectKeyInvalidKeyNameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("key"), TEXT("NotARealKey"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_key"), Params);

	TestFalse(TEXT("inject_key should fail with invalid key name"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectKeyInvalidActionTest,
	"Cortex.Editor.Input.InjectKey.ErrorWhenInvalidAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectKeyInvalidActionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("key"), TEXT("W"));
	Params->SetStringField(TEXT("action"), TEXT("invalid_action"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_key"), Params);

	TestFalse(TEXT("inject_key should fail with invalid action"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectMouseMissingActionTest,
	"Cortex.Editor.Input.InjectMouse.ErrorWhenMissingAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectMouseMissingActionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_mouse"), Params);
	TestFalse(TEXT("inject_mouse should fail without action"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectMouseInvalidButtonTest,
	"Cortex.Editor.Input.InjectMouse.ErrorWhenInvalidButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectMouseInvalidButtonTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action"), TEXT("click"));
	Params->SetStringField(TEXT("button"), TEXT("invalid_button"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_mouse"), Params);
	TestFalse(TEXT("inject_mouse should fail with invalid button"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectMouseScrollMissingDeltaTest,
	"Cortex.Editor.Input.InjectMouse.ErrorWhenScrollMissingDelta",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectMouseScrollMissingDeltaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action"), TEXT("scroll"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_mouse"), Params);
	TestFalse(TEXT("inject_mouse scroll should fail without delta"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputActionMissingNameTest,
	"Cortex.Editor.Input.InjectInputAction.ErrorWhenMissingActionName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputActionMissingNameTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_action"), Params);
	TestFalse(TEXT("inject_input_action should fail without action_name"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectSequenceInvalidKindTest,
	"Cortex.Editor.Input.InjectInputSequence.ErrorWhenInvalidStepKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectSequenceInvalidKindTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Steps;
	TSharedPtr<FJsonObject> StepObj = MakeShared<FJsonObject>();
	StepObj->SetNumberField(TEXT("at_ms"), 0.0);
	StepObj->SetStringField(TEXT("kind"), TEXT("invalid_kind"));
	Steps.Add(MakeShared<FJsonValueObject>(StepObj));
	Params->SetArrayField(TEXT("steps"), Steps);

	FDeferredResponseCallback Callback = [](FCortexCommandResult) {};
	const FCortexCommandResult Result = Handler.Execute(
		TEXT("inject_input_sequence"), Params, MoveTemp(Callback));

	TestFalse(TEXT("inject_input_sequence should fail with invalid kind"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputContinuousNoPIETest,
	"Cortex.Editor.Input.InjectInputContinuous.ErrorWhenNoPIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputContinuousNoPIETest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action_name"), TEXT("IA_Move"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_continuous"), Params);

	TestFalse(TEXT("inject_input_continuous should fail without PIE"), Result.bSuccess);
	TestEqual(TEXT("Error should be PIE_NOT_ACTIVE"), Result.ErrorCode, TEXT("PIE_NOT_ACTIVE"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputContinuousMissingActionTest,
	"Cortex.Editor.Input.InjectInputContinuous.ErrorWhenMissingActionName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputContinuousMissingActionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_continuous"), Params);

	TestFalse(TEXT("inject_input_continuous should fail without action_name"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputContinuousInvalidModeTest,
	"Cortex.Editor.Input.InjectInputContinuous.ErrorWhenModeInvalid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputContinuousInvalidModeTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action_name"), TEXT("IA_Move"));
	Params->SetStringField(TEXT("mode"), TEXT("hold"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_continuous"), Params);

	TestFalse(TEXT("inject_input_continuous should reject an unknown mode"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputContinuousBadDurationTest,
	"Cortex.Editor.Input.InjectInputContinuous.ErrorWhenDurationNotNumeric",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputContinuousBadDurationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action_name"), TEXT("IA_Move"));
	Params->SetStringField(TEXT("duration_ms"), TEXT("soon"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_continuous"), Params);

	TestFalse(TEXT("inject_input_continuous should reject a non-numeric duration_ms"), Result.bSuccess);
	TestEqual(TEXT("Error should be INVALID_FIELD"), Result.ErrorCode, TEXT("INVALID_FIELD"));

	return true;
}

// The command schema advertised `action` while the code read only `action_name`, so a
// caller following the schema was rejected outright. These two assert the alias is
// accepted: the request must get PAST field validation and fail on PIE instead.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputContinuousActionAliasTest,
	"Cortex.Editor.Input.InjectInputContinuous.AcceptsActionAlias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputContinuousActionAliasTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action"), TEXT("IA_Move"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_continuous"), Params);

	TestFalse(TEXT("still fails without PIE"), Result.bSuccess);
	TestEqual(
		TEXT("`action` alias must pass validation and reach the PIE check, not INVALID_FIELD"),
		Result.ErrorCode,
		TEXT("PIE_NOT_ACTIVE"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInjectInputActionAliasTest,
	"Cortex.Editor.Input.InjectInputAction.AcceptsActionAlias",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexEditorInjectInputActionAliasTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("action"), TEXT("IA_Move"));

	const FCortexCommandResult Result = Handler.Execute(TEXT("inject_input_action"), Params);

	TestFalse(TEXT("still fails without PIE"), Result.bSuccess);
	TestEqual(
		TEXT("`action` alias must pass validation and reach the PIE check, not INVALID_FIELD"),
		Result.ErrorCode,
		TEXT("PIE_NOT_ACTIVE"));

	return true;
}

// Every case above is negative: it asserts a request is REJECTED. A suite made only of
// rejections cannot tell a working command from one that was never registered, and the
// defect this change fixes is precisely a command that reported success while doing
// nothing. This case is the positive half. It asserts the registration itself - by exact
// name and exact schema - and it asserts its own premise first, so an empty or broken
// command list fails here instead of passing silently.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEditorInputCommandsAreRegisteredTest,
	"Cortex.Editor.Input.Registration.ExactSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

namespace
{
const FCortexCommandInfo* FindCortexCommand(const TArray<FCortexCommandInfo>& Commands, const TCHAR* Name)
{
	return Commands.FindByPredicate(
		[Name](const FCortexCommandInfo& Info) { return Info.Name == Name; });
}

/** Renders a command's params as "name:type:required" so one TestEqual pins the whole schema. */
FString DescribeCortexParams(const FCortexCommandInfo& Info)
{
	TArray<FString> Parts;
	Parts.Reserve(Info.Params.Num());
	for (const FCortexParamInfo& Param : Info.Params)
	{
		Parts.Add(FString::Printf(
			TEXT("%s:%s:%s"),
			*Param.Name,
			*Param.Type,
			Param.bRequired ? TEXT("required") : TEXT("optional")));
	}
	return FString::Join(Parts, TEXT(","));
}
}

bool FCortexEditorInputCommandsAreRegisteredTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FCortexEditorCommandHandler Handler;
	const TArray<FCortexCommandInfo> Commands = Handler.GetSupportedCommands();

	// Premise, asserted rather than assumed: the handler really did advertise a command set,
	// and it still contains a command that predates this change.
	if (!TestTrue(TEXT("Editor handler advertises at least one command"), Commands.Num() > 0))
	{
		return false;
	}
	if (!TestNotNull(
			TEXT("Control: the pre-existing inject_key command is advertised"),
			FindCortexCommand(Commands, TEXT("inject_key"))))
	{
		return false;
	}

	const FCortexCommandInfo* Continuous = FindCortexCommand(Commands, TEXT("inject_input_continuous"));
	if (!TestNotNull(TEXT("inject_input_continuous is advertised"), Continuous))
	{
		return false;
	}
	TestEqual(
		TEXT("inject_input_continuous schema"),
		DescribeCortexParams(*Continuous),
		TEXT("action_name:string:required,value:object:optional,mode:string:optional,duration_ms:number:optional"));

	// The single-shot command's required param is renamed by this change; the MCP fallback
	// schema is generated from it, so a drift here is a silently wrong client contract.
	const FCortexCommandInfo* Single = FindCortexCommand(Commands, TEXT("inject_input_action"));
	if (!TestNotNull(TEXT("inject_input_action is advertised"), Single))
	{
		return false;
	}
	TestEqual(
		TEXT("inject_input_action schema"),
		DescribeCortexParams(*Single),
		TEXT("action_name:string:required,value:object:optional,trigger_event:string:optional"));

	return true;
}
