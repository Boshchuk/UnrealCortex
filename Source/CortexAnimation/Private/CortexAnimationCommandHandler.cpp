#include "CortexAnimationCommandHandler.h"
#include "CortexCommandRouter.h"
#include "Operations/CortexAnimInspectOps.h"
#include "Operations/CortexAnimAuthorOps.h"

FCortexCommandResult FCortexAnimationCommandHandler::Execute(
	const FString& Command,
	const TSharedPtr<FJsonObject>& Params,
	FDeferredResponseCallback DeferredCallback)
{
	(void)DeferredCallback;

	if (Command == TEXT("list_assets"))
	{
		return FCortexAnimInspectOps::ListAssets(Params);
	}
	if (Command == TEXT("get_sequence_info"))
	{
		return FCortexAnimInspectOps::GetSequenceInfo(Params);
	}
	if (Command == TEXT("get_montage_info"))
	{
		return FCortexAnimInspectOps::GetMontageInfo(Params);
	}
	if (Command == TEXT("get_skeleton_info"))
	{
		return FCortexAnimInspectOps::GetSkeletonInfo(Params);
	}
	if (Command == TEXT("get_animbp_info"))
	{
		return FCortexAnimInspectOps::GetAnimBlueprintInfo(Params);
	}

	// Phase B — authoring (mutating).
	if (Command == TEXT("add_notify"))
	{
		return FCortexAnimAuthorOps::AddNotify(Params);
	}
	if (Command == TEXT("remove_notify"))
	{
		return FCortexAnimAuthorOps::RemoveNotify(Params);
	}
	if (Command == TEXT("add_curve"))
	{
		return FCortexAnimAuthorOps::AddCurve(Params);
	}
	if (Command == TEXT("set_curve_keys"))
	{
		return FCortexAnimAuthorOps::SetCurveKeys(Params);
	}
	if (Command == TEXT("remove_curve"))
	{
		return FCortexAnimAuthorOps::RemoveCurve(Params);
	}
	if (Command == TEXT("add_montage_section"))
	{
		return FCortexAnimAuthorOps::AddMontageSection(Params);
	}
	if (Command == TEXT("remove_montage_section"))
	{
		return FCortexAnimAuthorOps::RemoveMontageSection(Params);
	}
	if (Command == TEXT("add_socket"))
	{
		return FCortexAnimAuthorOps::AddSocket(Params);
	}
	if (Command == TEXT("set_socket_transform"))
	{
		return FCortexAnimAuthorOps::SetSocketTransform(Params);
	}
	if (Command == TEXT("remove_socket"))
	{
		return FCortexAnimAuthorOps::RemoveSocket(Params);
	}

	return FCortexCommandRouter::Error(
		CortexErrorCodes::UnknownCommand,
		FString::Printf(TEXT("Unknown anim command: %s"), *Command));
}

TArray<FCortexCommandInfo> FCortexAnimationCommandHandler::GetSupportedCommands() const
{
	return {
		FCortexCommandInfo{ TEXT("list_assets"), TEXT("List animation assets (AnimSequence/AnimMontage/AnimBlueprint/Skeleton)") }
			.Optional(TEXT("asset_type"), TEXT("string"), TEXT("Filter: sequence | montage | animbp | skeleton | all (default all)"))
			.Optional(TEXT("path_filter"), TEXT("string"), TEXT("Restrict to a content path, e.g. /Game/Characters"))
			.Optional(TEXT("query"), TEXT("string"), TEXT("Case-insensitive substring match on name/path"))
			.Optional(TEXT("limit"), TEXT("number"), TEXT("Max results (default 100)")),
		FCortexCommandInfo{ TEXT("get_sequence_info"), TEXT("Read an AnimSequence: length, frame rate, key count, skeleton, notifies") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimSequence asset path")),
		FCortexCommandInfo{ TEXT("get_montage_info"), TEXT("Read an AnimMontage: sections, slot tracks, notifies") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimMontage asset path")),
		FCortexCommandInfo{ TEXT("get_skeleton_info"), TEXT("Read a Skeleton: bones and sockets") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("Skeleton asset path")),
		FCortexCommandInfo{ TEXT("get_animbp_info"), TEXT("Read an AnimBlueprint: target skeleton, state machines and their states") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimBlueprint asset path")),

		// Phase B — authoring (mutating, undo-wrapped).
		FCortexCommandInfo{ TEXT("add_notify"), TEXT("Add a named notify to an AnimSequence") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimSequence asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Notify name"))
			.Required(TEXT("time"), TEXT("number"), TEXT("Trigger time in seconds"))
			.Optional(TEXT("duration"), TEXT("number"), TEXT("Duration for a notify-state (default 0 = instant)")),
		FCortexCommandInfo{ TEXT("remove_notify"), TEXT("Remove all notifies with a given name from an AnimSequence") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimSequence asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Notify name to remove")),
		FCortexCommandInfo{ TEXT("add_curve"), TEXT("Add an (empty) float curve to an AnimSequence") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimSequence asset path"))
			.Required(TEXT("curve_name"), TEXT("string"), TEXT("Float curve name")),
		FCortexCommandInfo{ TEXT("set_curve_keys"), TEXT("Set (replace) the keys of a float curve on an AnimSequence; creates the curve if missing") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimSequence asset path"))
			.Required(TEXT("curve_name"), TEXT("string"), TEXT("Float curve name"))
			.Required(TEXT("keys"), TEXT("array"), TEXT("Array of {time, value} objects")),
		FCortexCommandInfo{ TEXT("remove_curve"), TEXT("Remove a float curve from an AnimSequence") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimSequence asset path"))
			.Required(TEXT("curve_name"), TEXT("string"), TEXT("Float curve name")),
		FCortexCommandInfo{ TEXT("add_montage_section"), TEXT("Add a composite section to an AnimMontage") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimMontage asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Section name (must be unique)"))
			.Optional(TEXT("start_time"), TEXT("number"), TEXT("Section start time in seconds (default 0)")),
		FCortexCommandInfo{ TEXT("remove_montage_section"), TEXT("Remove a composite section from an AnimMontage by name") }
			.Required(TEXT("asset_path"), TEXT("string"), TEXT("AnimMontage asset path"))
			.Required(TEXT("name"), TEXT("string"), TEXT("Section name to remove")),
		FCortexCommandInfo{ TEXT("add_socket"), TEXT("Add a socket to a Skeleton, attached to a bone") }
			.Required(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path"))
			.Required(TEXT("socket_name"), TEXT("string"), TEXT("Socket name (must be unique)"))
			.Required(TEXT("bone_name"), TEXT("string"), TEXT("Parent bone name (must exist)"))
			.Optional(TEXT("location"), TEXT("array"), TEXT("Relative location [x,y,z] (default 0,0,0)"))
			.Optional(TEXT("rotation"), TEXT("array"), TEXT("Relative rotation [roll,pitch,yaw] (default 0,0,0)"))
			.Optional(TEXT("scale"), TEXT("array"), TEXT("Relative scale [x,y,z] (default 1,1,1)")),
		FCortexCommandInfo{ TEXT("set_socket_transform"), TEXT("Update an existing skeleton socket's relative transform") }
			.Required(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path"))
			.Required(TEXT("socket_name"), TEXT("string"), TEXT("Socket name"))
			.Optional(TEXT("location"), TEXT("array"), TEXT("Relative location [x,y,z]"))
			.Optional(TEXT("rotation"), TEXT("array"), TEXT("Relative rotation [roll,pitch,yaw]"))
			.Optional(TEXT("scale"), TEXT("array"), TEXT("Relative scale [x,y,z]")),
		FCortexCommandInfo{ TEXT("remove_socket"), TEXT("Remove a socket from a Skeleton by name") }
			.Required(TEXT("skeleton_path"), TEXT("string"), TEXT("Skeleton asset path"))
			.Required(TEXT("socket_name"), TEXT("string"), TEXT("Socket name to remove")),
	};
}
