#include "CortexStateTreeCommandHandler.h"

#include "CortexCommandRouter.h"
#include "CortexTypes.h"

FCortexCommandResult FCortexStateTreeCommandHandler::Execute(
	const FString& Command,
	const TSharedPtr<FJsonObject>& Params,
	FDeferredResponseCallback DeferredCallback)
{
	(void)Params;
	(void)DeferredCallback;

	return FCortexCommandRouter::Error(
		CortexErrorCodes::UnknownCommand,
		FString::Printf(TEXT("Unknown StateTree command: %s"), *Command));
}

TArray<FCortexCommandInfo> FCortexStateTreeCommandHandler::GetSupportedCommands() const
{
	return {
		FCortexCommandInfo{ TEXT("list_assets"), TEXT("List StateTree assets") }
			.Optional(TEXT("path_filter"), TEXT("string"), TEXT("Content path prefix"))
			.Optional(TEXT("limit"), TEXT("number"), TEXT("Maximum assets to return")),
		FCortexCommandInfo{ TEXT("create_asset"), TEXT("Create a StateTree asset") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Destination asset path"))
			.Required(TEXT("schema_class"), TEXT("string"), TEXT("StateTree schema class path or name"))
			.Optional(TEXT("root_name"), TEXT("string"), TEXT("Root state display name"))
			.Optional(TEXT("save"), TEXT("boolean"), TEXT("Persist package after creation")),
		FCortexCommandInfo{ TEXT("duplicate_asset"), TEXT("Duplicate a StateTree asset") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Source asset path"))
			.Required(TEXT("new_asset_path"), TEXT("string"), TEXT("Destination asset path"))
			.Optional(TEXT("save"), TEXT("boolean"), TEXT("Persist package after duplication")),
		FCortexCommandInfo{ TEXT("delete_asset"), TEXT("Delete a StateTree asset") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("dry_run"), TEXT("boolean"), TEXT("Report referencers without deleting"))
			.Optional(TEXT("force"), TEXT("boolean"), TEXT("Delete despite referencers"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("dump_tree"), TEXT("Serialize a StateTree") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("include_transitions"), TEXT("boolean"), TEXT("Include transitions"))
			.Optional(TEXT("include_nodes"), TEXT("boolean"), TEXT("Include read-only node metadata")),
		FCortexCommandInfo{ TEXT("get_state"), TEXT("Get one StateTree state") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("state_id"), TEXT("string"), TEXT("State GUID"))
			.Optional(TEXT("state_path"), TEXT("string"), TEXT("State path")),
		FCortexCommandInfo{ TEXT("check_structure"), TEXT("Run read-only StateTree structure checks") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path")),
		FCortexCommandInfo{ TEXT("validate_asset"), TEXT("Run mutating StateTree validation fixups") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("save"), TEXT("boolean"), TEXT("Persist package after validation"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("compile"), TEXT("Compile a StateTree") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("save"), TEXT("boolean"), TEXT("Persist package after compile"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("add_state"), TEXT("Add a StateTree state") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("New state display name"))
			.Optional(TEXT("parent_state_id"), TEXT("string"), TEXT("Parent state GUID"))
			.Optional(TEXT("parent_state_path"), TEXT("string"), TEXT("Parent state path"))
			.Optional(TEXT("index"), TEXT("number"), TEXT("Insert index under parent"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("remove_state"), TEXT("Remove a StateTree state") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("state_id"), TEXT("string"), TEXT("State GUID"))
			.Optional(TEXT("state_path"), TEXT("string"), TEXT("State path"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("rename_state"), TEXT("Rename a StateTree state") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Required(TEXT("new_name"), TEXT("string"), TEXT("New display name"))
			.Optional(TEXT("state_id"), TEXT("string"), TEXT("State GUID"))
			.Optional(TEXT("state_path"), TEXT("string"), TEXT("State path"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("move_state"), TEXT("Move a StateTree state") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("state_id"), TEXT("string"), TEXT("State GUID"))
			.Optional(TEXT("state_path"), TEXT("string"), TEXT("State path"))
			.Optional(TEXT("new_parent_state_id"), TEXT("string"), TEXT("New parent state GUID"))
			.Optional(TEXT("new_parent_state_path"), TEXT("string"), TEXT("New parent state path"))
			.Optional(TEXT("index"), TEXT("number"), TEXT("Insert index under the new parent"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("set_state_properties"), TEXT("Set whitelisted StateTree state properties") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Optional(TEXT("state_id"), TEXT("string"), TEXT("State GUID"))
			.Optional(TEXT("state_path"), TEXT("string"), TEXT("State path"))
			.Required(TEXT("properties"), TEXT("object"), TEXT("State properties patch"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("add_transition"), TEXT("Add a simple StateTree transition") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Required(TEXT("source_state_id"), TEXT("string"), TEXT("Source state GUID"))
			.Required(TEXT("target_state_id"), TEXT("string"), TEXT("Target state GUID"))
			.Optional(TEXT("trigger"), TEXT("string"), TEXT("Transition trigger enum name"))
			.Optional(TEXT("event_tag"), TEXT("string"), TEXT("Optional Gameplay Tag for event transitions"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("remove_transition"), TEXT("Remove a StateTree transition") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Required(TEXT("source_state_id"), TEXT("string"), TEXT("Source state GUID"))
			.Required(TEXT("transition_id"), TEXT("string"), TEXT("Transition GUID"))
			.OptionalExpectedFingerprint(),
		FCortexCommandInfo{ TEXT("set_transition_properties"), TEXT("Set simple StateTree transition properties") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Asset path"))
			.Required(TEXT("source_state_id"), TEXT("string"), TEXT("Source state GUID"))
			.Required(TEXT("transition_id"), TEXT("string"), TEXT("Transition GUID"))
			.Required(TEXT("properties"), TEXT("object"), TEXT("Transition properties patch"))
			.OptionalExpectedFingerprint(),
	};
}
