#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexGraphCommandHandler.h"
#include "Dom/JsonObject.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexGraphDescribeNodeTest,
	"Cortex.Graph.DescribeNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
bool ParamListContainsName(const TSharedPtr<FJsonObject>& Contract, const FString& ListField, const FString& ParamName)
{
	const TArray<TSharedPtr<FJsonValue>>* Params = nullptr;
	if (!Contract.IsValid() || !Contract->TryGetArrayField(ListField, Params) || Params == nullptr)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Params)
	{
		const TSharedPtr<FJsonObject>* ParamObj = nullptr;
		if (Value.IsValid() && Value->TryGetObject(ParamObj) && ParamObj && (*ParamObj).IsValid())
		{
			FString Name;
			if ((*ParamObj)->TryGetStringField(TEXT("name"), Name) && Name == ParamName)
			{
				return true;
			}
		}
	}
	return false;
}
}

bool FCortexGraphDescribeNodeTest::RunTest(const FString& Parameters)
{
	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("graph"), TEXT("Cortex Graph"), TEXT("1.0.1"),
		MakeShared<FCortexGraphCommandHandler>());

	auto Describe = [&Router, this](const FString& NodeClass) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("node_class"), NodeClass);
		FCortexCommandResult Result = Router.Execute(TEXT("graph.describe_node"), Params);
		TestTrue(TEXT("describe_node succeeds"), Result.bSuccess);
		return Result.bSuccess && Result.Data.IsValid() ? Result.Data : nullptr;
	};

	{
		TSharedPtr<FJsonObject> Contract = Describe(TEXT("UK2Node_VariableGet"));
		TestTrue(TEXT("VariableGet contract present"), Contract.IsValid());
		if (Contract.IsValid())
		{
			TestEqual(TEXT("canonical class"), Contract->GetStringField(TEXT("node_class")), FString(TEXT("UK2Node_VariableGet")));
			TestTrue(TEXT("supported flag"), Contract->GetBoolField(TEXT("supported")));
			TestTrue(TEXT("prerequisites mention is_variable"),
				Contract->GetStringField(TEXT("prerequisites")).Contains(TEXT("is_variable=true")));
			TestTrue(TEXT("non-retryable errors include VARIABLE_NOT_FOUND"),
				Contract->GetStringField(TEXT("non_retryable_errors")).Contains(TEXT("VARIABLE_NOT_FOUND")));
		}
	}

	{
		TSharedPtr<FJsonObject> Contract = Describe(TEXT("UK2Node_SwitchEnum"));
		TestTrue(TEXT("SwitchEnum contract present"), Contract.IsValid());
		if (Contract.IsValid())
		{
			TestTrue(TEXT("SwitchEnum documents enum_name"),
				ParamListContainsName(Contract, TEXT("optional_params"), TEXT("enum_name")));
		}
	}

	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("node_class"), TEXT("UK2Node_NotASupportedClass"));
		FCortexCommandResult Result = Router.Execute(TEXT("graph.describe_node"), Params);
		TestFalse(TEXT("unsupported class fails"), Result.bSuccess);
	}

	return true;
}
