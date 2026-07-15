#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexTypes.h"
#include "CortexQACommandHandler.h"

namespace
{
    FCortexCommandRouter CreateQARouterPerf()
    {
        FCortexCommandRouter Router;
        Router.RegisterDomain(TEXT("qa"), TEXT("Cortex QA"), TEXT("1.0.2"),
            MakeShared<FCortexQACommandHandler>());
        return Router;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexQASamplePerformanceNoPIETest,
    "Cortex.QA.Perf.SamplePerformance.NoPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexQASamplePerformanceNoPIETest::RunTest(const FString& Parameters)
{
    FCortexCommandRouter Router = CreateQARouterPerf();
    const FCortexCommandResult Result = Router.Execute(TEXT("qa.sample_performance"), MakeShared<FJsonObject>());

    TestFalse(TEXT("sample_performance should fail when PIE is not active"), Result.bSuccess);
    TestEqual(TEXT("sample_performance should return PIE_NOT_ACTIVE"), Result.ErrorCode, CortexErrorCodes::PIENotActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexQAAssertFpsNoPIETest,
    "Cortex.QA.Perf.AssertFps.NoPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexQAAssertFpsNoPIETest::RunTest(const FString& Parameters)
{
    FCortexCommandRouter Router = CreateQARouterPerf();
    TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
    Params->SetNumberField(TEXT("min_fps"), 30.0);

    const FCortexCommandResult Result = Router.Execute(TEXT("qa.assert_fps"), Params);

    TestFalse(TEXT("assert_fps should fail when PIE is not active"), Result.bSuccess);
    TestEqual(TEXT("assert_fps should return PIE_NOT_ACTIVE"), Result.ErrorCode, CortexErrorCodes::PIENotActive);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexQAAssertFpsMissingParamTest,
    "Cortex.QA.Perf.AssertFps.MissingParam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexQAAssertFpsMissingParamTest::RunTest(const FString& Parameters)
{
    FCortexCommandRouter Router = CreateQARouterPerf();
    // No min_fps — param validation must fire before the PIE gate.
    const FCortexCommandResult Result = Router.Execute(TEXT("qa.assert_fps"), MakeShared<FJsonObject>());

    TestFalse(TEXT("assert_fps without min_fps should fail"), Result.bSuccess);
    TestEqual(TEXT("assert_fps without min_fps should return INVALID_FIELD"), Result.ErrorCode, CortexErrorCodes::InvalidField);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexQAAssertFrametimeMissingParamTest,
    "Cortex.QA.Perf.AssertFrametime.MissingParam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexQAAssertFrametimeMissingParamTest::RunTest(const FString& Parameters)
{
    FCortexCommandRouter Router = CreateQARouterPerf();
    // No max_ms — param validation must fire before the PIE gate.
    const FCortexCommandResult Result = Router.Execute(TEXT("qa.assert_frametime"), MakeShared<FJsonObject>());

    TestFalse(TEXT("assert_frametime without max_ms should fail"), Result.bSuccess);
    TestEqual(TEXT("assert_frametime without max_ms should return INVALID_FIELD"), Result.ErrorCode, CortexErrorCodes::InvalidField);
    return true;
}
