#include "CortexAnimationCommandHandler.h"
#include "CortexCommandRouter.h"
#include "Operations/CortexAnimInspectOps.h"

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
	};
}
