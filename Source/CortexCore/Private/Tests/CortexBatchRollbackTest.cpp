#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexBatchScope.h"
#include "CortexCoreCommandHandler.h"
#include "Dom/JsonObject.h"

namespace
{
class FCortexRollbackProbeHandler : public ICortexDomainHandler
{
public:
	virtual FCortexCommandResult Execute(
		const FString& Command,
		const TSharedPtr<FJsonObject>& Params,
		FDeferredResponseCallback DeferredCallback = nullptr) override
	{
		(void)Params;
		(void)DeferredCallback;
		if (Command == TEXT("create_node"))
		{
			if (FCortexCommandRouter::IsInBatch())
			{
				FCortexBatchScope::RegisterRollbackEntry(
					TEXT("add_node"),
					TEXT("ProbeNode_0"),
					TEXT("test add_node"),
					[]() { return true; },
					[]() { return true; });
			}
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("node_id"), TEXT("ProbeNode_0"));
			return FCortexCommandRouter::Success(Data);
		}
		if (Command == TEXT("fail_step"))
		{
			return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, TEXT("deliberate failure"));
		}
		return FCortexCommandRouter::Error(CortexErrorCodes::UnknownCommand, TEXT("unknown"));
	}

	virtual TArray<FCortexCommandInfo> GetSupportedCommands() const override
	{
		return {
			FCortexCommandInfo{ TEXT("create_node"), TEXT("probe") },
			FCortexCommandInfo{ TEXT("fail_step"), TEXT("probe") },
		};
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBatchRollbackTest,
	"Cortex.Core.Batch.Rollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBatchRollbackTest::RunTest(const FString& Parameters)
{
	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("probe"), TEXT("Probe"), TEXT("1.0.1"),
		MakeShared<FCortexRollbackProbeHandler>());

	auto AddStep = [](TSharedPtr<FJsonObject>& BatchParams, const FString& Command) -> void
	{
		TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
		Step->SetStringField(TEXT("command"), Command);
		Step->SetObjectField(TEXT("params"), MakeShared<FJsonObject>());
		const TArray<TSharedPtr<FJsonValue>>* Existing = nullptr;
		TArray<TSharedPtr<FJsonValue>> Steps;
		if (BatchParams->TryGetArrayField(TEXT("commands"), Existing) && Existing != nullptr)
		{
			Steps = *Existing;
		}
		Steps.Add(MakeShared<FJsonValueObject>(Step));
		BatchParams->SetArrayField(TEXT("commands"), Steps);
	};

	TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
	Empty->SetArrayField(TEXT("commands"), TArray<TSharedPtr<FJsonValue>>());
	FCortexCommandResult EmptyResult = Router.Execute(TEXT("batch"), Empty);
	TestFalse(TEXT("zero-command batch rejected"), EmptyResult.bSuccess);
	TestEqual(TEXT("empty batch code"), EmptyResult.ErrorCode, CortexErrorCodes::InvalidInvocationShape);

	TSharedPtr<FJsonObject> Batch = MakeShared<FJsonObject>();
	Batch->SetBoolField(TEXT("stop_on_error"), true);
	Batch->SetBoolField(TEXT("rollback_on_error"), true);
	Batch->SetBoolField(TEXT("verify_rollback"), true);
	AddStep(Batch, TEXT("probe.create_node"));
	AddStep(Batch, TEXT("probe.fail_step"));
	FCortexCommandResult Result = Router.Execute(TEXT("batch"), Batch);
	TestFalse(TEXT("failing rollback batch returns failure"), Result.bSuccess);
	if (Result.ErrorDetails.IsValid())
	{
		const TSharedPtr<FJsonObject>* RollbackObj = nullptr;
		TestTrue(TEXT("rollback object present"), Result.ErrorDetails->TryGetObjectField(TEXT("rollback"), RollbackObj) && RollbackObj != nullptr);
		if (RollbackObj != nullptr)
		{
			TestTrue(TEXT("rollback attempted"), (*RollbackObj)->GetBoolField(TEXT("attempted")));
			TestTrue(TEXT("rollback verified"), (*RollbackObj)->GetBoolField(TEXT("verified")));
		}
		const TArray<TSharedPtr<FJsonValue>>* Created = nullptr;
		TestTrue(TEXT("created_node_ids present"), Result.ErrorDetails->TryGetArrayField(TEXT("created_node_ids"), Created));
		if (Created && Created->Num() > 0)
		{
			TestEqual(TEXT("created node id recorded"), (*Created)[0]->AsString(), FString(TEXT("ProbeNode_0")));
		}
		const TArray<TSharedPtr<FJsonValue>>* Residual = nullptr;
		TestTrue(TEXT("residual_changes present"), Result.ErrorDetails->TryGetArrayField(TEXT("residual_changes"), Residual));
		if (Residual)
		{
			TestEqual(TEXT("no residual changes"), Residual->Num(), 0);
		}
	}

	TSharedPtr<FJsonObject> Unverified = MakeShared<FJsonObject>();
	Unverified->SetBoolField(TEXT("stop_on_error"), true);
	Unverified->SetBoolField(TEXT("rollback_on_error"), true);
	Unverified->SetBoolField(TEXT("verify_rollback"), false);
	AddStep(Unverified, TEXT("probe.create_node"));
	AddStep(Unverified, TEXT("probe.fail_step"));
	FCortexCommandResult UnverifiedResult = Router.Execute(TEXT("batch"), Unverified);
	TestFalse(TEXT("unverified batch fails"), UnverifiedResult.bSuccess);
	TestEqual(TEXT("dirty editor state code"), UnverifiedResult.ErrorCode, CortexErrorCodes::DirtyEditorState);

	// Tail-case rollback: stop_on_error=false so all steps run, a later step fails, and the
	// post-loop scan must still roll back every node created anywhere in the batch.
	TSharedPtr<FJsonObject> Tail = MakeShared<FJsonObject>();
	Tail->SetBoolField(TEXT("stop_on_error"), false);
	Tail->SetBoolField(TEXT("rollback_on_error"), true);
	Tail->SetBoolField(TEXT("verify_rollback"), true);
	AddStep(Tail, TEXT("probe.create_node"));
	AddStep(Tail, TEXT("probe.fail_step"));
	AddStep(Tail, TEXT("probe.create_node"));
	FCortexCommandResult TailResult = Router.Execute(TEXT("batch"), Tail);
	TestFalse(TEXT("tail-case batch with any failure returns failure"), TailResult.bSuccess);
	if (TailResult.ErrorDetails.IsValid())
	{
		const TSharedPtr<FJsonObject>* TailRollback = nullptr;
		TestTrue(TEXT("tail-case rollback object present"),
			TailResult.ErrorDetails->TryGetObjectField(TEXT("rollback"), TailRollback) && TailRollback != nullptr);
		if (TailRollback != nullptr)
		{
			TestTrue(TEXT("tail-case rollback attempted"), (*TailRollback)->GetBoolField(TEXT("attempted")));
			TestTrue(TEXT("tail-case rollback verified"), (*TailRollback)->GetBoolField(TEXT("verified")));
		}
		const TArray<TSharedPtr<FJsonValue>>* TailCreated = nullptr;
		TestTrue(TEXT("tail-case created_node_ids present"),
			TailResult.ErrorDetails->TryGetArrayField(TEXT("created_node_ids"), TailCreated));
		if (TailCreated)
		{
			TestEqual(TEXT("both created nodes rolled back"), TailCreated->Num(), 2);
		}
		const TArray<TSharedPtr<FJsonValue>>* TailResidual = nullptr;
		TestTrue(TEXT("tail-case residual_changes present"),
			TailResult.ErrorDetails->TryGetArrayField(TEXT("residual_changes"), TailResidual));
		if (TailResidual)
		{
			TestEqual(TEXT("tail-case no residual changes"), TailResidual->Num(), 0);
		}
	}

	return true;
}
