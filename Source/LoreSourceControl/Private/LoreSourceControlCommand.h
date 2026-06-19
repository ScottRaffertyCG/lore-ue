#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h"
#include "Misc/IQueuedWork.h"

class ISourceControlOperation;
class ILoreSourceControlWorker;

/**
 * Encapsulates one source-control operation so it can run on the engine thread
 * pool. Connection parameters are snapshotted on the game thread at creation so
 * the worker thread never touches the provider/settings concurrently.
 */
class FLoreSourceControlCommand : public IQueuedWork
{
public:
	FLoreSourceControlCommand(
		const TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe>& InOperation,
		const TSharedRef<class ILoreSourceControlWorker, ESPMode::ThreadSafe>& InWorker,
		const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete());

	/** Performs the work; called from DoThreadedWork or directly when synchronous. */
	bool DoWork();

	/** IQueuedWork interface */
	virtual void Abandon() override;
	virtual void DoThreadedWork() override;

	/** Save results and fire the completion delegate. Game thread only. */
	ECommandResult::Type ReturnResults();

public:
	/** Snapshotted connection parameters (safe to use off the game thread). */
	FString LoreBinaryPath;
	FString WorkingCopyRoot;
	FString RepositoryUrl;
	FString UserName;

	/** The outward-facing operation (carries parameters and results). */
	TSharedRef<class ISourceControlOperation, ESPMode::ThreadSafe> Operation;

	/** The worker that actually performs the operation. */
	TSharedRef<class ILoreSourceControlWorker, ESPMode::ThreadSafe> Worker;

	/** Completion delegate. */
	FSourceControlOperationComplete OperationCompleteDelegate;

	/** Set to 1 once the worker thread has finished Execute(). */
	volatile int32 bExecuteProcessed = 0;

	/** True if the operation succeeded. */
	bool bCommandSuccessful = false;

	/** If true, this command frees itself in Tick(). */
	bool bAutoDelete = false;

	/** Concurrency mode. */
	EConcurrency::Type Concurrency = EConcurrency::Synchronous;

	/** Files this operation acts on (absolute paths). */
	TArray<FString> Files;

	/** Process exit code from the last CLI invocation. */
	int32 ReturnCode = 0;

	/** Info/warning messages. */
	TArray<FString> InfoMessages;

	/** Error messages. */
	TArray<FString> ErrorMessages;
};
