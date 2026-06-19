#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

class FLoreSourceControlCommand;

/**
 * Interface for a unit of source-control work. Each registered UE operation
 * (Connect, UpdateStatus, CheckOut, ...) maps to one worker.
 */
class ILoreSourceControlWorker
{
public:
	virtual ~ILoreSourceControlWorker() = default;

	/** Operation name this worker handles (matches ISourceControlOperation::GetName()). */
	virtual FName GetName() const = 0;

	/** Does the actual work. Runs on a worker thread (or synchronously). */
	virtual bool Execute(FLoreSourceControlCommand& InCommand) = 0;

	/** Merges results into the provider state cache. Always runs on the game thread. */
	virtual bool UpdateStates() const = 0;
};

typedef TSharedRef<ILoreSourceControlWorker, ESPMode::ThreadSafe> FLoreSourceControlWorkerRef;
