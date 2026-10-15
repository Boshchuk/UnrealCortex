#pragma once

#include "CortexVersionCompat.h"
#include "Misc/AutomationTest.h"

// Keep the value/lifecycle checks running on 5.4, where only the public
// registration query is missing. Never inspect the protected engine map.
template <typename SubsystemType, typename ActionType>
void CortexTestContinuousInputInjection(
    FAutomationTestBase& Test, const TCHAR* What, SubsystemType& Subsystem,
    const ActionType* Action, bool bExpected)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
    Test.AddInfo(FString::Printf(TEXT("skipped: needs UE 5.6 public continuous injection query: %s"), What));
#else
    Test.TestEqual(What, CortexHasContinuousInputInjection(Subsystem, Action), bExpected);
#endif
}
