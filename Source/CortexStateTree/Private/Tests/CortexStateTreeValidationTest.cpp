#include "Misc/AutomationTest.h"
#include "CortexSTTypes.h"
#include "CortexStateTreeCommandHandler.h"
#include "CortexStateTreeTestUtils.h"
#include "CortexTypes.h"
#include "Operations/CortexSTAssetOps.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeTypes.h"

namespace
{
UStateTreeState* GetRootState(UStateTree* StateTree)
{
	if (StateTree == nullptr)
	{
		return nullptr;
	}

	const UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
	if (EditorData == nullptr || EditorData->SubTrees.Num() == 0)
	{
		return nullptr;
	}

	return EditorData->SubTrees[0];
}

bool GetValidationFlag(const FCortexCommandResult& Result, bool& bOutValid)
{
	if (!Result.Data.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Validation = nullptr;
	return Result.Data->TryGetObjectField(TEXT("validation"), Validation)
		&& Validation != nullptr
		&& Validation->IsValid()
		&& (*Validation)->TryGetBoolField(TEXT("valid"), bOutValid);
}

bool GetFingerprintDirtyFlag(const TSharedPtr<FJsonObject>& Data, bool& bOutDirty)
{
	if (!Data.IsValid())
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* Fingerprint = nullptr;
	return Data->TryGetObjectField(TEXT("fingerprint"), Fingerprint)
		&& Fingerprint != nullptr
		&& Fingerprint->IsValid()
		&& (*Fingerprint)->TryGetBoolField(TEXT("is_dirty"), bOutDirty);
}

bool GetDirectFingerprintDirtyFlag(const TSharedPtr<FJsonObject>& Fingerprint, bool& bOutDirty)
{
	return Fingerprint.IsValid() && Fingerprint->TryGetBoolField(TEXT("is_dirty"), bOutDirty);
}

void DeleteWithCurrentFingerprint(FCortexStateTreeCommandHandler& Handler, const FString& AssetPath)
{
	if (UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *AssetPath))
	{
		TSharedPtr<FJsonObject> DeleteParams = CortexStateTreeTest::Params();
		DeleteParams->SetStringField(TEXT("asset_path"), AssetPath);
		DeleteParams->SetObjectField(TEXT("expected_fingerprint"), CortexST::MakeFingerprint(StateTree));
		(void)Handler.Execute(TEXT("delete_asset"), DeleteParams);
		return;
	}

	CortexStateTreeTest::DeleteIfLoaded(AssetPath);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeValidationCommandsTest,
	"Cortex.StateTree.Validation.Commands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeValidationCommandsTest::RunTest(const FString& Parameters)
{
	FCortexStateTreeCommandHandler Handler;
	const FString AssetPath = CortexStateTreeTest::MakeAssetPath(TEXT("ST_Validation"));

	TSharedPtr<FJsonObject> CreateParams = CortexStateTreeTest::Params();
	CreateParams->SetStringField(TEXT("asset_path"), AssetPath);
	CreateParams->SetStringField(TEXT("schema_class"), CortexStateTreeTest::GetTestSchemaClassPath());

	const FCortexCommandResult Create = Handler.Execute(TEXT("create_asset"), CreateParams);
	TestTrue(TEXT("create succeeds"), Create.bSuccess);

	TSharedPtr<FJsonObject> CheckParams = CortexStateTreeTest::Params();
	CheckParams->SetStringField(TEXT("asset_path"), AssetPath);

	const FCortexCommandResult Check = Handler.Execute(TEXT("check_structure"), CheckParams);
	TestTrue(TEXT("check_structure succeeds"), Check.bSuccess);
	TestTrue(TEXT("check_structure returns validation"),
		Check.Data.IsValid() && Check.Data->HasTypedField<EJson::Object>(TEXT("validation")));

	TSharedPtr<FJsonObject> ValidateWithoutFingerprint = CortexStateTreeTest::Params();
	ValidateWithoutFingerprint->SetStringField(TEXT("asset_path"), AssetPath);

	const FCortexCommandResult ValidateFail = Handler.Execute(TEXT("validate_asset"), ValidateWithoutFingerprint);
	TestFalse(TEXT("validate_asset without fingerprint fails"), ValidateFail.bSuccess);
	TestEqual(TEXT("validate_asset without fingerprint uses stale precondition"),
		ValidateFail.ErrorCode,
		CortexErrorCodes::StalePrecondition);

	TSharedPtr<FJsonObject> ValidateParams = CortexStateTreeTest::Params();
	ValidateParams->SetStringField(TEXT("asset_path"), AssetPath);
	if (Create.Data.IsValid() && Create.Data->HasTypedField<EJson::Object>(TEXT("fingerprint")))
	{
		ValidateParams->SetObjectField(TEXT("expected_fingerprint"), Create.Data->GetObjectField(TEXT("fingerprint")));
	}

	const FCortexCommandResult Validate = Handler.Execute(TEXT("validate_asset"), ValidateParams);
	TestTrue(TEXT("validate_asset with fingerprint succeeds"), Validate.bSuccess);
	TestTrue(TEXT("validate_asset returns fingerprint"),
		Validate.Data.IsValid() && Validate.Data->HasTypedField<EJson::Object>(TEXT("fingerprint")));

	TSharedPtr<FJsonObject> CompileParams = CortexStateTreeTest::Params();
	CompileParams->SetStringField(TEXT("asset_path"), AssetPath);
	if (Validate.Data.IsValid() && Validate.Data->HasTypedField<EJson::Object>(TEXT("fingerprint")))
	{
		CompileParams->SetObjectField(TEXT("expected_fingerprint"), Validate.Data->GetObjectField(TEXT("fingerprint")));
	}

	const FCortexCommandResult Compile = Handler.Execute(TEXT("compile"), CompileParams);
	TestTrue(TEXT("compile with fingerprint succeeds"), Compile.bSuccess);
	TestTrue(TEXT("compile returns diagnostics"),
		Compile.Data.IsValid() && Compile.Data->HasTypedField<EJson::Array>(TEXT("diagnostics")));

	CortexStateTreeTest::DeleteIfLoaded(AssetPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeValidationNonGotoTransitionTest,
	"Cortex.StateTree.Validation.NonGotoTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeValidationNonGotoTransitionTest::RunTest(const FString& Parameters)
{
	FCortexStateTreeCommandHandler Handler;
	const FString AssetPath = CortexStateTreeTest::MakeAssetPath(TEXT("ST_ValidationNonGoto"));

	TSharedPtr<FJsonObject> CreateParams = CortexStateTreeTest::Params();
	CreateParams->SetStringField(TEXT("asset_path"), AssetPath);
	CreateParams->SetStringField(TEXT("schema_class"), CortexStateTreeTest::GetTestSchemaClassPath());

	const FCortexCommandResult Create = Handler.Execute(TEXT("create_asset"), CreateParams);
	TestTrue(TEXT("create succeeds"), Create.bSuccess);

	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *AssetPath);
	TestNotNull(TEXT("StateTree loads"), StateTree);
	UStateTreeState* RootState = GetRootState(StateTree);
	TestNotNull(TEXT("root state exists"), RootState);
	if (StateTree == nullptr || RootState == nullptr)
	{
		CortexStateTreeTest::DeleteIfLoaded(AssetPath);
		return false;
	}

	StateTree->Modify();
	RootState->Modify();
	FStateTreeTransition& CompletionTransition = RootState->AddTransition(
		EStateTreeTransitionTrigger::OnStateCompleted,
		EStateTreeTransitionType::Succeeded,
		nullptr);
	CompletionTransition.State.LinkType = EStateTreeTransitionType::Succeeded;
	CompletionTransition.State.ID.Invalidate();
	CompletionTransition.State.Name = NAME_None;
	StateTree->MarkPackageDirty();

	TSharedPtr<FJsonObject> CheckParams = CortexStateTreeTest::Params();
	CheckParams->SetStringField(TEXT("asset_path"), AssetPath);
	const FCortexCommandResult Check = Handler.Execute(TEXT("check_structure"), CheckParams);
	TestTrue(TEXT("check_structure succeeds"), Check.bSuccess);

	bool bValid = false;
	TestTrue(TEXT("check_structure returns validation.valid"), GetValidationFlag(Check, bValid));
	TestTrue(TEXT("non-goto completion transition stays valid in check_structure"), bValid);

	TSharedPtr<FJsonObject> ValidateParams = CortexStateTreeTest::Params();
	ValidateParams->SetStringField(TEXT("asset_path"), AssetPath);
	ValidateParams->SetObjectField(TEXT("expected_fingerprint"), CortexST::MakeFingerprint(StateTree));

	const FCortexCommandResult Validate = Handler.Execute(TEXT("validate_asset"), ValidateParams);
	TestTrue(TEXT("validate_asset succeeds"), Validate.bSuccess);
	bValid = false;
	TestTrue(TEXT("validate_asset returns validation.valid"), GetValidationFlag(Validate, bValid));
	TestTrue(TEXT("non-goto completion transition stays valid in validate_asset"), bValid);

	CortexStateTreeTest::DeleteIfLoaded(AssetPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeCompileMarksDirtyOnValidationFixupTest,
	"Cortex.StateTree.Validation.CompileMarksDirtyOnValidationFixup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeCompileMarksDirtyOnValidationFixupTest::RunTest(const FString& Parameters)
{
	FCortexStateTreeCommandHandler Handler;
	const FString AssetPath = CortexStateTreeTest::MakeAssetPath(TEXT("ST_CompileFixup"));

	TSharedPtr<FJsonObject> CreateParams = CortexStateTreeTest::Params();
	CreateParams->SetStringField(TEXT("asset_path"), AssetPath);
	CreateParams->SetStringField(TEXT("schema_class"), CortexStateTreeTest::GetTestSchemaClassPath());

	const FCortexCommandResult Create = Handler.Execute(TEXT("create_asset"), CreateParams);
	TestTrue(TEXT("create succeeds"), Create.bSuccess);

	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *AssetPath);
	TestNotNull(TEXT("StateTree loads"), StateTree);
	UStateTreeState* RootState = GetRootState(StateTree);
	TestNotNull(TEXT("root state exists"), RootState);
	if (StateTree == nullptr || RootState == nullptr)
	{
		CortexStateTreeTest::DeleteIfLoaded(AssetPath);
		return false;
	}

	StateTree->Modify();
	RootState->Modify();
	UStateTreeState& ChildState = RootState->AddChildState(TEXT("Child"), EStateTreeStateType::State);
	ChildState.Modify();
	StateTree->MarkPackageDirty();

	const FCortexCommandResult SaveResult = FCortexSTAssetOps::SaveAsset(AssetPath);
	TestTrue(TEXT("save fixture succeeds"), SaveResult.bSuccess);
	if (!SaveResult.bSuccess)
	{
		CortexStateTreeTest::DeleteIfLoaded(AssetPath);
		return false;
	}

	TestFalse(TEXT("saved fixture package is clean"), StateTree->GetOutermost()->IsDirty());

	RootState->ClearFlags(RF_Transactional);
	ChildState.ClearFlags(RF_Transactional);
	TestFalse(TEXT("root state transactional flag cleared before compile"), RootState->HasAnyFlags(RF_Transactional));
	TestFalse(TEXT("child state transactional flag cleared before compile"), ChildState.HasAnyFlags(RF_Transactional));

	const TSharedPtr<FJsonObject> ExpectedFingerprint = CortexST::MakeFingerprint(StateTree);
	bool bExpectedDirty = true;
	TestTrue(TEXT("pre-compile fingerprint present"), GetDirectFingerprintDirtyFlag(ExpectedFingerprint, bExpectedDirty));
	TestFalse(TEXT("pre-compile fingerprint remains clean before compile"), bExpectedDirty);
	TestFalse(TEXT("fixture package remains clean before compile"), StateTree->GetOutermost()->IsDirty());

	TSharedPtr<FJsonObject> CompileParams = CortexStateTreeTest::Params();
	CompileParams->SetStringField(TEXT("asset_path"), AssetPath);
	CompileParams->SetObjectField(TEXT("expected_fingerprint"), ExpectedFingerprint);

	const FCortexCommandResult Compile = Handler.Execute(TEXT("compile"), CompileParams);
	TestTrue(TEXT("compile succeeds after validation fixup"), Compile.bSuccess);
	TestTrue(TEXT("compile fixup restores root transactional flag"), RootState->HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("compile fixup restores child transactional flag"), ChildState.HasAnyFlags(RF_Transactional));
	TestTrue(TEXT("compile fixup marks package dirty"), StateTree->GetOutermost()->IsDirty());

	bool bReturnedDirty = false;
	TestTrue(TEXT("compile returns fingerprint with dirty flag"), GetFingerprintDirtyFlag(Compile.Data, bReturnedDirty));
	TestTrue(TEXT("compile returns dirty fingerprint after validation fixup"), bReturnedDirty);

	DeleteWithCurrentFingerprint(Handler, AssetPath);
	return true;
}
