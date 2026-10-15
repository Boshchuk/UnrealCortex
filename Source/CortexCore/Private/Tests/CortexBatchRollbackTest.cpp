#include "Misc/AutomationTest.h"
#include "CortexCommandRouter.h"
#include "CortexBatchScope.h"
#include "CortexCoreCommandHandler.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"

namespace
{
class FCortexRollbackProbeHandler : public ICortexDomainHandler
{
public:
	explicit FCortexRollbackProbeHandler(UPackage* InAffectedPackage = nullptr)
		: AffectedPackage(InAffectedPackage)
	{
	}

	virtual FCortexCommandResult Execute(
		const FString& Command,
		const TSharedPtr<FJsonObject>& Params,
		FDeferredResponseCallback DeferredCallback = nullptr) override
	{
		(void)Params;
		(void)DeferredCallback;
		if (Command == TEXT("create_node"))
		{
			if (AffectedPackage.IsValid())
			{
				AffectedPackage->SetDirtyFlag(true);
			}
			if (FCortexCommandRouter::IsInBatch())
			{
				FCortexBatchScope::RegisterRollbackEntry(
					TEXT("add_node"),
					TEXT("ProbeNode_0"),
					TEXT("test add_node"),
					[]() { return true; },
					[]() { return true; },
					AffectedPackage);
			}
			TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
			Data->SetStringField(TEXT("node_id"), TEXT("ProbeNode_0"));
			return FCortexCommandRouter::Success(Data);
		}
		if (Command == TEXT("fail_step"))
		{
			return FCortexCommandRouter::Error(CortexErrorCodes::InvalidField, TEXT("deliberate failure"));
		}
		if (Command == TEXT("compile"))
		{
			++CompileCallCount;
			return FCortexCommandRouter::Success(MakeShared<FJsonObject>());
		}
		if (Command == TEXT("save_all"))
		{
			++SaveCallCount;
			return FCortexCommandRouter::Success(MakeShared<FJsonObject>());
		}
		if (Command == TEXT("unsafe_mutation"))
		{
			++UnsafeMutationCallCount;
			return FCortexCommandRouter::Success(MakeShared<FJsonObject>());
		}
		return FCortexCommandRouter::Error(CortexErrorCodes::UnknownCommand, TEXT("unknown"));
	}

	virtual TArray<FCortexCommandInfo> GetSupportedCommands() const override
	{
		return {
			FCortexCommandInfo{ TEXT("create_node"), TEXT("probe") }.RollbackSafe(),
			FCortexCommandInfo{ TEXT("fail_step"), TEXT("probe") }.RollbackSafe(),
			FCortexCommandInfo{ TEXT("compile"), TEXT("probe") },
			FCortexCommandInfo{ TEXT("save_all"), TEXT("probe") },
			FCortexCommandInfo{ TEXT("unsafe_mutation"), TEXT("probe") },
		};
	}

	int32 CompileCallCount = 0;
	int32 SaveCallCount = 0;
	int32 UnsafeMutationCallCount = 0;

private:
	TWeakObjectPtr<UPackage> AffectedPackage;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexBatchRollbackTest,
	"Cortex.Core.Batch.Rollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCortexBatchRollbackTest::RunTest(const FString& Parameters)
{
	UPackage* AffectedPackage = CreatePackage(TEXT("/Engine/Transient/CortexBatchRollbackDirtyPackage"));
	AffectedPackage->SetDirtyFlag(false);
	TSharedRef<FCortexRollbackProbeHandler> ProbeHandler = MakeShared<FCortexRollbackProbeHandler>(AffectedPackage);
	FCortexCommandRouter Router;
	Router.RegisterDomain(TEXT("probe"), TEXT("Probe"), TEXT("1.0.1"),
		ProbeHandler);

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
	TestFalse(TEXT("verified rollback restores a package that was clean before the batch"),
		AffectedPackage->IsDirty());
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

	// Persistence and compilation are forbidden throughout rollback-enabled batches. Otherwise a
	// later failure can roll back memory while leaving the pre-failure graph persisted to disk, and
	// compilation can reconstruct nodes/pins retained by the rollback journal.
	TSharedPtr<FJsonObject> CompileBeforeFailure = MakeShared<FJsonObject>();
	CompileBeforeFailure->SetBoolField(TEXT("stop_on_error"), false);
	CompileBeforeFailure->SetBoolField(TEXT("rollback_on_error"), true);
	CompileBeforeFailure->SetBoolField(TEXT("verify_rollback"), true);
	AddStep(CompileBeforeFailure, TEXT("probe.create_node"));
	AddStep(CompileBeforeFailure, TEXT("probe.compile"));
	AddStep(CompileBeforeFailure, TEXT("probe.save_all"));
	AddStep(CompileBeforeFailure, TEXT("probe.fail_step"));
	FCortexCommandResult PersistenceResult = Router.Execute(TEXT("batch"), CompileBeforeFailure);
	TestFalse(TEXT("rollback batch containing persistence operations fails"), PersistenceResult.bSuccess);
	TestEqual(TEXT("compile is never executed in rollback-enabled batches"), ProbeHandler->CompileCallCount, 0);
	TestEqual(TEXT("save_all is never executed in rollback-enabled batches"), ProbeHandler->SaveCallCount, 0);

	TSharedPtr<FJsonObject> BlockedOnly = MakeShared<FJsonObject>();
	BlockedOnly->SetBoolField(TEXT("stop_on_error"), true);
	BlockedOnly->SetBoolField(TEXT("rollback_on_error"), true);
	BlockedOnly->SetBoolField(TEXT("verify_rollback"), true);
	AddStep(BlockedOnly, TEXT("probe.create_node"));
	AddStep(BlockedOnly, TEXT("probe.compile"));
	FCortexCommandResult BlockedOnlyResult = Router.Execute(TEXT("batch"), BlockedOnly);
	TestFalse(TEXT("blocked compile is sufficient to fail and roll back the batch"), BlockedOnlyResult.bSuccess);
	TestEqual(TEXT("blocked-only compile is not executed"), ProbeHandler->CompileCallCount, 0);

	TSharedPtr<FJsonObject> UnmarkedCommand = MakeShared<FJsonObject>();
	UnmarkedCommand->SetBoolField(TEXT("stop_on_error"), true);
	UnmarkedCommand->SetBoolField(TEXT("rollback_on_error"), true);
	UnmarkedCommand->SetBoolField(TEXT("verify_rollback"), true);
	AddStep(UnmarkedCommand, TEXT("probe.create_node"));
	AddStep(UnmarkedCommand, TEXT("probe.unsafe_mutation"));
	FCortexCommandResult UnmarkedResult = Router.Execute(TEXT("batch"), UnmarkedCommand);
	TestFalse(TEXT("command not declared rollback-safe fails the batch"), UnmarkedResult.bSuccess);
	TestEqual(TEXT("unmarked command is rejected before execution"), ProbeHandler->UnsafeMutationCallCount, 0);

	// Tail-case rollback: stop_on_error=false so all steps run, a later step fails, and the
	// post-loop scan must still roll back every node created anywhere in the batch.
	TSharedPtr<FJsonObject> Tail = MakeShared<FJsonObject>();
	Tail->SetBoolField(TEXT("stop_on_error"), false);
	Tail->SetBoolField(TEXT("rollback_on_error"), true);
	Tail->SetBoolField(TEXT("verify_rollback"), true);
	AddStep(Tail, TEXT("probe.create_node"));
	AddStep(Tail, TEXT("probe.fail_step"));
	AddStep(Tail, TEXT("probe.compile"));
	FCortexCommandResult TailResult = Router.Execute(TEXT("batch"), Tail);
	TestFalse(TEXT("tail-case batch with any failure returns failure"), TailResult.bSuccess);
	TestEqual(TEXT("compile is blocked after an earlier rollback-batch failure"), ProbeHandler->CompileCallCount, 0);
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
			TestEqual(TEXT("created node rolled back"), TailCreated->Num(), 1);
		}
		const TArray<TSharedPtr<FJsonValue>>* TailResidual = nullptr;
		TestTrue(TEXT("tail-case residual_changes present"),
			TailResult.ErrorDetails->TryGetArrayField(TEXT("residual_changes"), TailResidual));
		if (TailResidual)
		{
			TestEqual(TEXT("tail-case no residual changes"), TailResidual->Num(), 0);
		}
	}

	AffectedPackage->SetDirtyFlag(false);
	AffectedPackage->MarkAsGarbage();
	return true;
}
