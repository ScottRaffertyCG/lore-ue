#include "LoreSourceControlCommand.h"
#include "ILoreSourceControlWorker.h"
#include "LoreSourceControlModule.h"
#include "LoreSourceControlProvider.h"
#include "ISourceControlOperation.h"
#include "HAL/PlatformProcess.h"

FLoreSourceControlCommand::FLoreSourceControlCommand(
	const TSharedRef<ISourceControlOperation, ESPMode::ThreadSafe>& InOperation,
	const TSharedRef<ILoreSourceControlWorker, ESPMode::ThreadSafe>& InWorker,
	const FSourceControlOperationComplete& InOperationCompleteDelegate)
	: Operation(InOperation)
	, Worker(InWorker)
	, OperationCompleteDelegate(InOperationCompleteDelegate)
{
	// Snapshot connection parameters on the game thread.
	check(IsInGameThread());
	FLoreSourceControlModule& Module = FLoreSourceControlModule::Get();
	const FLoreSourceControlProvider& Provider = Module.GetProvider();

	LoreBinaryPath = Provider.GetLoreBinaryPath();
	WorkingCopyRoot = Provider.GetWorkingCopyRoot();
	RepositoryUrl = Module.AccessSettings().GetRepositoryUrl();
	UserName = Provider.GetUserName();
}

bool FLoreSourceControlCommand::DoWork()
{
	bCommandSuccessful = Worker->Execute(*this);
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
	return bCommandSuccessful;
}

void FLoreSourceControlCommand::Abandon()
{
	FPlatformAtomics::InterlockedExchange(&bExecuteProcessed, 1);
}

void FLoreSourceControlCommand::DoThreadedWork()
{
	Concurrency = EConcurrency::Asynchronous;
	DoWork();
}

ECommandResult::Type FLoreSourceControlCommand::ReturnResults()
{
	// Merge state on the game thread, then notify.
	Worker->UpdateStates();

	const ECommandResult::Type Result = bCommandSuccessful ? ECommandResult::Succeeded : ECommandResult::Failed;
	OperationCompleteDelegate.ExecuteIfBound(Operation, Result);
	return Result;
}
