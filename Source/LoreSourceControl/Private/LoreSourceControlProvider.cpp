#include "LoreSourceControlProvider.h"
#include "LoreSourceControlCommand.h"
#include "LoreSourceControlModule.h"
#include "LoreSourceControlState.h"
#include "LoreSourceControlUtils.h"
#include "LoreSourceControlLog.h"
#include "ILoreSourceControlWorker.h"
#include "LorePathUtils.h"

#include "ISourceControlModule.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "ScopedSourceControlProgress.h"
#include "Logging/MessageLog.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Misc/QueuedThreadPool.h"
#include "HAL/PlatformProcess.h"
#include "UObject/Package.h"
#include "UObject/ObjectSaveContext.h"

#if SOURCE_CONTROL_WITH_SLATE
#include "SLoreSourceControlSettingsWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#endif

#define LOCTEXT_NAMESPACE "LoreSourceControl"

static FName ProviderName("Lore");

void FLoreSourceControlProvider::Init(bool bForceConnection)
{
	RefreshConnectionSettings();

	if (LoreBinaryPath.IsEmpty())
	{
		ConnectionState = ELoreConnectionState::Error;
		return;
	}

	ConnectionState = ELoreConnectionState::Offline;

	// The editor never re-queries status after a save, so hook package writes to
	// keep Content Browser overlays in sync. Init() is called repeatedly during
	// startup, so guard against registering more than once.
	if (!OnPackageSavedHandle.IsValid())
	{
		OnPackageSavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FLoreSourceControlProvider::HandlePackageSaved);
	}

	if (bForceConnection)
	{
		// A Connect operation will refine the connection state asynchronously.
		Execute(ISourceControlOperation::Create<FConnect>(), TArray<FString>(), EConcurrency::Synchronous);
	}
}

void FLoreSourceControlProvider::Close()
{
	if (OnPackageSavedHandle.IsValid())
	{
		UPackage::PackageSavedWithContextEvent.Remove(OnPackageSavedHandle);
		OnPackageSavedHandle.Reset();
	}

	PackagesToRefresh.Empty();
	StateCache.Empty();
	ConnectionState = ELoreConnectionState::Disconnected;
}

void FLoreSourceControlProvider::RefreshConnectionSettings()
{
	FLoreSourceControlModule& Module = FLoreSourceControlModule::Get();
	FLoreSourceControlSettings& Settings = Module.AccessSettings();

	const FString ConfiguredBinary = Settings.GetBinaryPath();
	LoreBinaryPath = LoreSourceControlUtils::FindLoreBinaryPath(ConfiguredBinary);

	// When the user hasn't pinned an explicit path, persist whatever we resolved
	// from PATH/bundled so the settings UI shows the detected `lore` executable.
	if (ConfiguredBinary.IsEmpty() && !LoreBinaryPath.IsEmpty())
	{
		Settings.SetBinaryPath(LoreBinaryPath);
		Settings.SaveSettings();
	}

	UserName = Settings.GetUserName();

	if (WorkingCopyRoot.IsEmpty())
	{
		WorkingCopyRoot = FLorePathUtils::NormalizeAbsolutePath(FPaths::ProjectDir());
	}
}

TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe> FLoreSourceControlProvider::GetStateInternal(const FString& Filename)
{
	const FString Key = FLorePathUtils::MakeCacheKey(Filename);
	if (TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe>* State = StateCache.Find(Key))
	{
		return *State;
	}

	TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe> NewState = MakeShared<FLoreSourceControlState, ESPMode::ThreadSafe>(Filename);
	StateCache.Add(Key, NewState);
	return NewState;
}

bool FLoreSourceControlProvider::RemoveFileFromCache(const FString& Filename)
{
	return StateCache.Remove(FLorePathUtils::MakeCacheKey(Filename)) > 0;
}

void FLoreSourceControlProvider::HandlePackageSaved(const FString& InPackageFilename, UPackage* InPackage, FObjectPostSaveContext InObjectSaveContext)
{
	// Ignore saves during cooking/automation; only interactive edits matter here.
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}

	const FString AbsoluteFilename = FLorePathUtils::NormalizeAbsolutePath(FPaths::ConvertRelativePathToFull(InPackageFilename));

	const FString Root = WorkingCopyRoot.IsEmpty()
		? FLorePathUtils::NormalizeAbsolutePath(FPaths::ProjectDir())
		: WorkingCopyRoot;
	if (!FLorePathUtils::IsUnderWorkspace(AbsoluteFilename, Root))
	{
		return;
	}

	// Optimistically reflect the edit so the overlay flips immediately. A file
	// that was clean (or never scanned) is now locally modified; leave any
	// existing pending add/delete/conflict state alone and let the confirming
	// status pass (queued below, drained in Tick) report the authoritative state.
	TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe> State = GetStateInternal(AbsoluteFilename);
	if (State->WorkingState == ELoreWorkingState::Clean || State->WorkingState == ELoreWorkingState::Unknown)
	{
		State->WorkingState = ELoreWorkingState::Modified;
		State->TimeStamp = FDateTime::Now();
	}

	// PackageSavedWithContextEvent fires once per asset (hundreds for OFPA maps),
	// so coalesce into a single batched refresh on the next Tick.
	PackagesToRefresh.AddUnique(AbsoluteFilename);
}

FText FLoreSourceControlProvider::GetStatusText() const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("IsEnabled"), IsEnabled() ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "No"));
	Args.Add(TEXT("IsAvailable"), IsAvailable() ? LOCTEXT("Yes", "Yes") : LOCTEXT("No", "No"));
	Args.Add(TEXT("BinaryPath"), FText::FromString(LoreBinaryPath));
	Args.Add(TEXT("WorkingCopy"), FText::FromString(WorkingCopyRoot));
	Args.Add(TEXT("UserName"), FText::FromString(UserName));

	return FText::Format(LOCTEXT("ProviderStatusText", "Provider: Lore\nEnabled: {IsEnabled}\nAvailable: {IsAvailable}\nlore binary: {BinaryPath}\nWorking copy: {WorkingCopy}\nUser: {UserName}"), Args);
}

TMap<ISourceControlProvider::EStatus, FString> FLoreSourceControlProvider::GetStatus() const
{
	TMap<EStatus, FString> Result;
	Result.Add(EStatus::Enabled, IsEnabled() ? TEXT("Yes") : TEXT("No"));
	Result.Add(EStatus::Connected, (IsEnabled() && IsAvailable()) ? TEXT("Yes") : TEXT("No"));
	Result.Add(EStatus::Repository, WorkingCopyRoot);
	Result.Add(EStatus::User, UserName);
	return Result;
}

bool FLoreSourceControlProvider::IsEnabled() const
{
	return true;
}

bool FLoreSourceControlProvider::IsAvailable() const
{
	return !LoreBinaryPath.IsEmpty() && ConnectionState != ELoreConnectionState::Error;
}

const FName& FLoreSourceControlProvider::GetName() const
{
	return ProviderName;
}

ECommandResult::Type FLoreSourceControlProvider::GetState(const TArray<FString>& InFiles, TArray<FSourceControlStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	if (!IsEnabled())
	{
		return ECommandResult::Failed;
	}

	const TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	if (InStateCacheUsage == EStateCacheUsage::ForceUpdate)
	{
		Execute(ISourceControlOperation::Create<FUpdateStatus>(), AbsoluteFiles);
	}

	for (const FString& File : AbsoluteFiles)
	{
		OutState.Add(GetStateInternal(File));
	}

	return ECommandResult::Succeeded;
}

ECommandResult::Type FLoreSourceControlProvider::GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage)
{
	return ECommandResult::Failed;
}

TArray<FSourceControlStateRef> FLoreSourceControlProvider::GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const
{
	TArray<FSourceControlStateRef> Result;
	for (const auto& CacheItem : StateCache)
	{
		FSourceControlStateRef State = CacheItem.Value;
		if (Predicate(State))
		{
			Result.Add(State);
		}
	}
	return Result;
}

FDelegateHandle FLoreSourceControlProvider::RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged)
{
	return OnSourceControlStateChanged.Add(SourceControlStateChanged);
}

void FLoreSourceControlProvider::UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle)
{
	OnSourceControlStateChanged.Remove(Handle);
}

ECommandResult::Type FLoreSourceControlProvider::Execute(const FSourceControlOperationRef& InOperation, FSourceControlChangelistPtr InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency, const FSourceControlOperationComplete& InOperationCompleteDelegate)
{
	if (!IsEnabled())
	{
		InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	const TArray<FString> AbsoluteFiles = SourceControlHelpers::AbsoluteFilenames(InFiles);

	TSharedPtr<ILoreSourceControlWorker, ESPMode::ThreadSafe> Worker = CreateWorker(InOperation->GetName());
	if (!Worker.IsValid())
	{
		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("OperationName"), FText::FromName(InOperation->GetName()));
		Arguments.Add(TEXT("ProviderName"), FText::FromName(GetName()));
		const FText Message(FText::Format(LOCTEXT("UnsupportedOperation", "Operation '{OperationName}' not supported by revision control provider '{ProviderName}'"), Arguments));
		FMessageLog("SourceControl").Error(Message);
		InOperation->AddErrorMessge(Message);
		InOperationCompleteDelegate.ExecuteIfBound(InOperation, ECommandResult::Failed);
		return ECommandResult::Failed;
	}

	FLoreSourceControlCommand* Command = new FLoreSourceControlCommand(InOperation, Worker.ToSharedRef(), InOperationCompleteDelegate);
	Command->Files = AbsoluteFiles;

	if (InConcurrency == EConcurrency::Synchronous)
	{
		Command->bAutoDelete = false;
		return ExecuteSynchronousCommand(*Command, InOperation->GetInProgressString());
	}

	Command->bAutoDelete = true;
	return IssueCommand(*Command, false);
}

bool FLoreSourceControlProvider::CanExecuteOperation(const FSourceControlOperationRef& InOperation) const
{
	return WorkersMap.Find(InOperation->GetName()) != nullptr;
}

bool FLoreSourceControlProvider::CanCancelOperation(const FSourceControlOperationRef& InOperation) const
{
	// lore CLI processes run to completion or time out; there is no mid-flight
	// cancellation mechanism. Return false so the editor does not offer a cancel
	// button that would silently have no effect.
	return false;
}

void FLoreSourceControlProvider::CancelOperation(const FSourceControlOperationRef& InOperation)
{
	// No-op: cancellation is not supported (see CanCancelOperation).
}

bool FLoreSourceControlProvider::UsesLocalReadOnlyState() const
{
	// Lore does not manage the read-only bit on the working copy.
	return false;
}

bool FLoreSourceControlProvider::UsesChangelists() const
{
	return false;
}

bool FLoreSourceControlProvider::UsesUncontrolledChangelists() const
{
	return true;
}

bool FLoreSourceControlProvider::UsesCheckout() const
{
	return true;
}

bool FLoreSourceControlProvider::UsesFileRevisions() const
{
	return true;
}

bool FLoreSourceControlProvider::UsesSnapshots() const
{
	return false;
}

bool FLoreSourceControlProvider::AllowsDiffAgainstDepot() const
{
	// TryToDownloadFileFromBackgroundThread is not yet implemented.
	// FLoreSourceControlRevision::Get() materializes revisions for
	// explicit history-view diffs, but the background download hook
	// that this flag gates is unimplemented. Return false to avoid
	// triggering the unimplemented path.
	// Set to true once TryToDownloadFileFromBackgroundThread is wired up.
	return false;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
TOptional<bool> FLoreSourceControlProvider::IsAtLatestRevision() const
{
	return TOptional<bool>();
}

TOptional<int> FLoreSourceControlProvider::GetNumLocalChanges() const
{
	return TOptional<int>();
}
#endif
TSharedPtr<ILoreSourceControlWorker, ESPMode::ThreadSafe> FLoreSourceControlProvider::CreateWorker(const FName& InOperationName) const
{
	if (const FGetLoreSourceControlWorker* Operation = WorkersMap.Find(InOperationName))
	{
		return Operation->Execute();
	}
	return nullptr;
}

void FLoreSourceControlProvider::RegisterWorker(const FName& InName, const FGetLoreSourceControlWorker& InDelegate)
{
	WorkersMap.Add(InName, InDelegate);
}

void FLoreSourceControlProvider::OutputCommandMessages(const FLoreSourceControlCommand& InCommand) const
{
	FMessageLog SourceControlLog("SourceControl");

	for (const FString& Error : InCommand.ErrorMessages)
	{
		SourceControlLog.Error(FText::FromString(Error));
	}
	for (const FString& Info : InCommand.InfoMessages)
	{
		SourceControlLog.Info(FText::FromString(Info));
	}
}

void FLoreSourceControlProvider::Tick()
{
	bool bStatesUpdated = false;
	for (int32 CommandIndex = 0; CommandIndex < CommandQueue.Num(); ++CommandIndex)
	{
		FLoreSourceControlCommand& Command = *CommandQueue[CommandIndex];
		if (Command.bExecuteProcessed)
		{
			CommandQueue.RemoveAt(CommandIndex);

			bStatesUpdated |= Command.Worker->UpdateStates();

			// Refine the connection state from the result.
			if (Command.Operation->GetName() == "Connect" && !Command.bCommandSuccessful)
			{
				ConnectionState = ELoreConnectionState::Offline;
			}
			else if (Command.bCommandSuccessful)
			{
				ConnectionState = ELoreConnectionState::Connected;
			}

			OutputCommandMessages(Command);

			Command.ReturnResults();

			if (Command.bAutoDelete)
			{
				delete &Command;
			}

			// Only process one command per tick to avoid concurrent queue modification.
			break;
		}
	}

	if (bStatesUpdated)
	{
		OnSourceControlStateChanged.Broadcast();
	}

	// Drain any files saved since the last tick. Broadcasting now repaints the
	// optimistic "modified" overlay immediately; the async status pass then
	// confirms or corrects each file's true state (and broadcasts again when it
	// completes via the command-drain path above).
	if (PackagesToRefresh.Num() > 0)
	{
		TArray<FString> Files = MoveTemp(PackagesToRefresh);
		PackagesToRefresh.Reset();

		OnSourceControlStateChanged.Broadcast();

		if (IsAvailable())
		{
			Execute(ISourceControlOperation::Create<FUpdateStatus>(), Files, EConcurrency::Asynchronous);
		}
	}
}

TArray<TSharedRef<ISourceControlLabel>> FLoreSourceControlProvider::GetLabels(const FString& InMatchingSpec) const
{
	// Lore branches are not surfaced as labels in v1.
	return TArray<TSharedRef<ISourceControlLabel>>();
}

TArray<FSourceControlChangelistRef> FLoreSourceControlProvider::GetChangelists(EStateCacheUsage::Type InStateCacheUsage)
{
	return TArray<FSourceControlChangelistRef>();
}

#if SOURCE_CONTROL_WITH_SLATE
TSharedRef<class SWidget> FLoreSourceControlProvider::MakeSettingsWidget() const
{
	return SNew(SLoreSourceControlSettings);
}
#endif

ECommandResult::Type FLoreSourceControlProvider::ExecuteSynchronousCommand(FLoreSourceControlCommand& InCommand, const FText& Task)
{
	ECommandResult::Type Result = ECommandResult::Failed;

	{
		FScopedSourceControlProgress Progress(Task);

		IssueCommand(InCommand, false);

		while (!InCommand.bExecuteProcessed)
		{
			Tick();
			Progress.Tick();
			FPlatformProcess::Sleep(0.01f);
		}

		// One more Tick to drain the completed command.
		Tick();

		if (InCommand.bCommandSuccessful)
		{
			Result = ECommandResult::Succeeded;
		}
	}

	check(!InCommand.bAutoDelete);

	if (CommandQueue.Contains(&InCommand))
	{
		CommandQueue.Remove(&InCommand);
	}

	delete &InCommand;

	return Result;
}

ECommandResult::Type FLoreSourceControlProvider::IssueCommand(FLoreSourceControlCommand& InCommand, const bool bSynchronous)
{
	if (!bSynchronous && GThreadPool != nullptr)
	{
		GThreadPool->AddQueuedWork(&InCommand);
		CommandQueue.Add(&InCommand);
		return ECommandResult::Succeeded;
	}

	InCommand.bCommandSuccessful = InCommand.DoWork();

	InCommand.Worker->UpdateStates();

	OutputCommandMessages(InCommand);

	return InCommand.ReturnResults();
}

#undef LOCTEXT_NAMESPACE
