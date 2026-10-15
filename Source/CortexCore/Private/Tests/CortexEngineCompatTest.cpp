#include "Misc/AutomationTest.h"
#include "CortexEngineCompat.h"
#include "Dom/JsonObject.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEngineCompatJsonKeyTest,
	"Cortex.Core.EngineCompat.JsonKeyToString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexEngineCompatJsonKeyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("shared_key"), TEXT("value"));

	for (const auto& Pair : Object->Values)
	{
		TestEqual(TEXT("JSON key converts to an owning FString"),
			CortexEngineCompat::JsonKeyToString(Pair.Key), FString(TEXT("shared_key")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexEngineCompatStringTableTest,
	"Cortex.Core.EngineCompat.StringTableSourceString",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexEngineCompatStringTableTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UStringTable* Table = NewObject<UStringTable>(GetTransientPackage());
	CortexEngineCompat::SetStringTableSourceString(
		*Table->GetMutableStringTable(), TEXT("Key"), TEXT("Value"));

	FString SourceString;
	TestTrue(TEXT("StringTable entry exists"),
		Table->GetMutableStringTable()->GetSourceString(TEXT("Key"), SourceString));
	TestEqual(TEXT("StringTable source string is retained"), SourceString, FString(TEXT("Value")));
	Table->MarkAsGarbage();
	return true;
}
