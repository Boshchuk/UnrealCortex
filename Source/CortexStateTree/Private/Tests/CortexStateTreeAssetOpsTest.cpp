#include "Misc/AutomationTest.h"
#include "CortexStateTreeCommandHandler.h"
#include "CortexStateTreeTestUtils.h"
#include "CortexTypes.h"
#include "Dom/JsonObject.h"
#include "StateTree.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeCreateRequiresSchemaTest,
	"Cortex.StateTree.Asset.Create.RequiresSchema",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeCreateRequiresSchemaTest::RunTest(const FString& Parameters)
{
	FCortexStateTreeCommandHandler Handler;
	TSharedPtr<FJsonObject> Params = CortexStateTreeTest::Params();
	Params->SetStringField(TEXT("asset_path"), CortexStateTreeTest::MakeAssetPath(TEXT("ST_NoSchema")));

	FCortexCommandResult Result = Handler.Execute(TEXT("create_asset"), Params);
	TestFalse(TEXT("create without schema fails"), Result.bSuccess);
	TestEqual(TEXT("error code"), Result.ErrorCode, CortexErrorCodes::InvalidField);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCortexStateTreeAssetCrudTest,
	"Cortex.StateTree.Asset.Crud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter
)

bool FCortexStateTreeAssetCrudTest::RunTest(const FString& Parameters)
{
	FCortexStateTreeCommandHandler Handler;
	const FString AssetPath = CortexStateTreeTest::MakeAssetPath(TEXT("ST_CRUD"));
	const FString CopyPath = AssetPath + TEXT("_Copy");

	TSharedPtr<FJsonObject> CreateParams = CortexStateTreeTest::Params();
	CreateParams->SetStringField(TEXT("asset_path"), AssetPath);
	CreateParams->SetStringField(TEXT("schema_class"), CortexStateTreeTest::GetTestSchemaClassPath());
	CreateParams->SetStringField(TEXT("root_name"), TEXT("Root"));
	CreateParams->SetBoolField(TEXT("save"), false);
	FCortexCommandResult Create = Handler.Execute(TEXT("create_asset"), CreateParams);
	TestTrue(TEXT("create succeeds"), Create.bSuccess);
	TestNotNull(TEXT("created StateTree loads"), LoadObject<UStateTree>(nullptr, *AssetPath));

	TSharedPtr<FJsonObject> ListParams = CortexStateTreeTest::Params();
	ListParams->SetStringField(TEXT("path_filter"), TEXT("/Game/Temp"));
	FCortexCommandResult List = Handler.Execute(TEXT("list_assets"), ListParams);
	TestTrue(TEXT("list succeeds"), List.bSuccess);
	TestTrue(TEXT("list has assets"), List.Data.IsValid() && List.Data->HasTypedField<EJson::Array>(TEXT("assets")));

	TSharedPtr<FJsonObject> DuplicateParams = CortexStateTreeTest::Params();
	DuplicateParams->SetStringField(TEXT("asset_path"), AssetPath);
	DuplicateParams->SetStringField(TEXT("new_asset_path"), CopyPath);
	DuplicateParams->SetBoolField(TEXT("save"), false);
	FCortexCommandResult Duplicate = Handler.Execute(TEXT("duplicate_asset"), DuplicateParams);
	TestTrue(TEXT("duplicate succeeds"), Duplicate.bSuccess);
	TestNotNull(TEXT("copy loads"), LoadObject<UStateTree>(nullptr, *CopyPath));

	TSharedPtr<FJsonObject> DeleteParams = CortexStateTreeTest::Params();
	DeleteParams->SetStringField(TEXT("asset_path"), CopyPath);
	DeleteParams->SetBoolField(TEXT("dry_run"), true);
	FCortexCommandResult DryRun = Handler.Execute(TEXT("delete_asset"), DeleteParams);
	TestTrue(TEXT("dry run succeeds"), DryRun.bSuccess);
	bool bWouldDelete = false;
	DryRun.Data->TryGetBoolField(TEXT("would_delete"), bWouldDelete);
	TestTrue(TEXT("dry run reports would_delete"), bWouldDelete);

	CortexStateTreeTest::DeleteIfLoaded(CopyPath);
	CortexStateTreeTest::DeleteIfLoaded(AssetPath);
	return true;
}
