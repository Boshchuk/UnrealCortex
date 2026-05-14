#include "CortexFrontendShutdown.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FCortexFrontendShutdownSkipsMenuTraversalDuringEngineExitTest,
    "Cortex.Frontend.Module.ShutdownSkipsMenuTraversalDuringEngineExit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexFrontendShutdownSkipsMenuTraversalDuringEngineExitTest::RunTest(const FString& Parameters)
{
    (void)Parameters;

    TestFalse(
        TEXT("Shutdown should not traverse ToolMenus while engine exit is requested"),
        UE::CortexFrontend::ShouldTraverseToolMenusDuringShutdown(true, true));

    TestFalse(
        TEXT("Shutdown should not traverse ToolMenus when ToolMenus is unavailable"),
        UE::CortexFrontend::ShouldTraverseToolMenusDuringShutdown(false, false));

    TestTrue(
        TEXT("Shutdown can traverse ToolMenus during normal module unload"),
        UE::CortexFrontend::ShouldTraverseToolMenusDuringShutdown(false, true));

    return true;
}
