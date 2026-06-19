#pragma once

#include "CoreMinimal.h"
#include "CortexTypes.h"

/**
 * Runs work on the next core-ticker tick (the editor main loop) instead of inside
 * the current command-dispatch call stack, then delivers the result through the
 * deferred response channel.
 *
 * Why this exists: command handlers execute inside an AsyncTask(GameThread) task.
 * Heavy engine operations that synchronously pump/flush the task graph — FBX import,
 * bulk SavePackage, static-mesh build — re-enter a queue already being processed one
 * level up the stack and trip `++Queue.RecursionGuard == 1` (a fatal assertion that
 * takes the editor down, not a catchable error). Deferring such work to a one-shot
 * ticker puts the internal flush at top-of-stack, exactly like clicking the action in
 * the UI, so it is safe.
 */
class CORTEXCORE_API FCortexDeferredExec
{
public:
	/**
	 * Schedule Work to run on the next tick and deliver its result via Done.
	 *
	 * @param Work  The operation to run on the game thread next tick.
	 * @param Done  The deferred response callback supplied to the command handler.
	 *              If null (e.g. inside a batch, where deferral is unsupported), Work
	 *              runs inline and its result is returned directly.
	 * @return When deferred, a placeholder result with bIsDeferred=true (the real
	 *         result is delivered through Done). When run inline, the actual result.
	 */
	static FCortexCommandResult RunNextTick(
		TFunction<FCortexCommandResult()> Work,
		FDeferredResponseCallback Done);
};
