
#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

class UMaterial;

/**
 * One reversible mutation recorded while executing a batch.
 * Registered by domain operations (graph add_node/connect) via FCortexBatchScope
 * so a failing batch can undo every mutation it created, in reverse order.
 */
struct CORTEXCORE_API FCortexBatchRollbackEntry
{
	FString Kind;
	FString NodeId;
	FString Description;
	TFunction<bool()> Rollback;
	TFunction<bool()> Verify;
	// Package affected by this entry; verified rollback clears its dirty flag so a "successful"
	// rollback never leaves the asset marked modified when the graph is back to its prior state.
	TWeakObjectPtr<UPackage> Package;
};

/** Result of executing the rollback journal for a failing batch. */
struct CORTEXCORE_API FCortexBatchRollbackResult
{
	bool bAttempted = true;
	bool bVerified = true;
	TArray<FString> CreatedNodeIds;
	TArray<FString> ResidualChanges;
};

/**
 * RAII guard for batch execution.
 * Increments BatchDepth on construction, decrements on destruction.
 * On destruction, invokes all registered cleanup actions, then calls
 * PostEditChange + RebuildGraph for all dirty materials.
 */
class CORTEXCORE_API FCortexBatchScope
{
public:
	using FBatchCleanupCallback = TFunction<void()>;

	FCortexBatchScope();
	~FCortexBatchScope();

	// Non-copyable, non-movable
	FCortexBatchScope(const FCortexBatchScope&) = delete;
	FCortexBatchScope& operator=(const FCortexBatchScope&) = delete;

	/** Mark a material as needing PostEditChange on batch end. */
	static void MarkMaterialDirty(UMaterial* Material);

	/**
	 * Register a cleanup action to run when the outermost batch ends.
	 * Key-based deduplication: only the first callback per key is kept.
	 * Domain modules use this for deferred notifications (NotifyGraphChanged, etc.)
	 */
	static void AddCleanupAction(const FString& Key, FBatchCleanupCallback Callback);

	/**
	 * Register a reversible mutation (e.g. a node or connection created by the batch).
	 * Only recorded while inside a batch and before rollback has executed.
	 */
	static void RegisterRollbackEntry(
		const FString& Kind,
		const FString& NodeId,
		const FString& Description,
		TFunction<bool()> Rollback,
		TFunction<bool()> Verify,
		TWeakObjectPtr<UPackage> Package = nullptr);

	/**
	 * Execute the rollback journal in reverse registration order.
	 * Returns whether every mutation was reverted and verified; on full verification the
	 * affected packages' dirty flags are cleared so a later save cannot persist a no-op.
	 */
	static FCortexBatchRollbackResult ExecuteRollback();

	/** Discard recorded rollback entries without executing them. */
	static void DiscardRollbackEntries();

	/** Snapshot the set of packages already dirty at the start of the outermost batch. */
	static void CaptureDirtyBaseline();

private:
	/** Materials that need PostEditChange when batch ends. */
	static TSet<TWeakObjectPtr<UMaterial>> DirtyMaterials;

	/** Generic cleanup actions keyed for deduplication. */
	static TMap<FString, FBatchCleanupCallback> CleanupActions;

	/** Reverse-order rollback journal for the current batch. */
	static TArray<FCortexBatchRollbackEntry> RollbackEntries;

	/** True once rollback has executed for the current outermost batch. */
	static bool bRollbackExecuted;

	/** Packages already dirty before the outermost batch began; a verified rollback must never clear these. */
	static TSet<UPackage*> PackagesDirtyBeforeBatch;
};
