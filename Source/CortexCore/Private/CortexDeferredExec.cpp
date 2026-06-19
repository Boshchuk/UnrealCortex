#include "CortexDeferredExec.h"
#include "Containers/Ticker.h"

FCortexCommandResult FCortexDeferredExec::RunNextTick(
	TFunction<FCortexCommandResult()> Work,
	FDeferredResponseCallback Done)
{
	// No deferred channel available (e.g. inside a batch) — run inline so the caller
	// still gets a result rather than a dangling deferred placeholder.
	if (!Done)
	{
		return Work ? Work() : FCortexCommandResult{};
	}

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[Work = MoveTemp(Work), Callback = MoveTemp(Done)](float /*DeltaTime*/) mutable -> bool
			{
				FCortexCommandResult Result = Work ? Work() : FCortexCommandResult{};
				if (Callback)
				{
					Callback(Result);
				}
				return false; // one-shot
			}),
		0.0f);

	FCortexCommandResult Deferred;
	Deferred.bIsDeferred = true;
	return Deferred;
}
