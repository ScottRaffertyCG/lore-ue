#include "LoreSourceControlOperations.h"
#include "LoreSourceControlCommand.h"
#include "LoreSourceControlModule.h"
#include "LoreSourceControlProvider.h"
#include "LoreSourceControlUtils.h"
#include "LoreSourceControlLog.h"
#include "LorePathUtils.h"

#include "SourceControlOperations.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "LoreSourceControl.Operations"

namespace
{
	/**
	 * Build the workspace-relative paths Lore expects for a set of absolute files.
	 * Files outside the working copy (engine content, instanced temp packages,
	 * etc.) are dropped: forwarding them would make `lore` fail with "invalid path".
	 */
	TArray<FString> ToRelativePaths(const TArray<FString>& InAbsolute, const FString& InRoot)
	{
		TArray<FString> Out;
		Out.Reserve(InAbsolute.Num());
		for (const FString& File : InAbsolute)
		{
			if (!FLorePathUtils::IsUnderWorkspace(File, InRoot))
			{
				UE_LOG(LogLoreSourceControl, Verbose, TEXT("Skipping out-of-workspace path: %s"), *File);
				continue;
			}
			FString Rel = FLorePathUtils::ToWorkspaceRelativePath(File, InRoot);
			if (!Rel.IsEmpty())
			{
				Out.Add(Rel);
			}
		}
		return Out;
	}

	/**
	 * Refresh per-file states by running `lore status [--scan]` plus
	 * `lore lock query`, then (optionally) `lore file history` per file.
	 * Files that exist but aren't reported by status are recorded as clean.
	 */
	bool RefreshFileStates(
		FLoreSourceControlCommand& Command,
		bool bScan,
		bool bWithHistory,
		TArray<FLoreSourceControlState>& OutStates,
		TMap<FString, TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>>>* OutHistory)
	{
		const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(Command);

		// 1) Status.
		TArray<FString> StatusParams;
		if (bScan)
		{
			StatusParams.Add(TEXT("--scan"));
		}

		const TArray<FString> RelativeFiles = ToRelativePaths(Command.Files, Command.WorkingCopyRoot);

		TArray<FString> StatusResults;
		const bool bStatusOk = LoreSourceControlUtils::RunCommandBatched(
			TEXT("status"), StatusParams, RelativeFiles, Context, /*bUseTargetsFile=*/true,
			StatusResults, Command.ErrorMessages);

		FLoreRepoStatus RepoStatus;
		LoreSourceControlUtils::ParseStatusResults(StatusResults, Command.WorkingCopyRoot, RepoStatus, OutStates);

		// 2) Fill clean states for requested files not reported as changed. This
		// MUST happen before locks are applied: a file can be locked while
		// otherwise unmodified (status reports nothing), so it would not yet have
		// a state for the lock to attach to. Creating the clean state first lets
		// step 3 stamp the correct lock state onto it.
		{
			TSet<FString> Reported;
			for (const FLoreSourceControlState& State : OutStates)
			{
				Reported.Add(FLorePathUtils::MakeCacheKey(State.LocalFilename));
			}

			for (const FString& File : Command.Files)
			{
				if (!FLorePathUtils::IsUnderWorkspace(File, Command.WorkingCopyRoot))
				{
					continue;
				}
				const FString Key = FLorePathUtils::MakeCacheKey(File);
				if (!Reported.Contains(Key) && FPaths::FileExists(File))
				{
					FLoreSourceControlState Clean(File);
					Clean.DepotFilename = FLorePathUtils::ToWorkspaceRelativePath(File, Command.WorkingCopyRoot);
					Clean.WorkingState = ELoreWorkingState::Clean;
					Clean.LockState = ELoreLockState::NotLocked;
					Clean.LocalRevision = RepoStatus.LocalRevision;
					Clean.LocalRevisionNumber = RepoStatus.LocalRevisionNumber;
					Clean.bNewerVersionOnServer = RepoStatus.bRemoteAhead;
					Clean.TimeStamp = FDateTime::Now();
					OutStates.Add(MoveTemp(Clean));
				}
			}
		}

		// 3) Locks. Applied after clean states exist so locks on unmodified files
		// are reflected (LockedByMe / LockedByOther) rather than silently dropped.
		{
			TArray<FString> LockResults;
			int32 LockReturn = -1;
			LoreSourceControlUtils::RunCommand(TEXT("lock query"), TArray<FString>(), TArray<FString>(), Context, LockResults, Command.ErrorMessages, LockReturn);

			TArray<FLoreLockInfo> Locks;
			LoreSourceControlUtils::ParseLockQueryResults(LockResults, Locks);
			LoreSourceControlUtils::ApplyLocksToStates(Locks, Command.UserName, Command.WorkingCopyRoot, OutStates);
		}

		// Diagnostic: report what we computed for each requested file so we can see
		// whether modified/lock state is being detected and cached as expected.
		// Verbose by default; enable with `log LogLoreSourceControl Verbose`.
		UE_LOG(LogLoreSourceControl, Verbose, TEXT("RefreshFileStates (scan=%d): %d requested, %d states computed"),
			bScan ? 1 : 0, Command.Files.Num(), OutStates.Num());
		for (const FLoreSourceControlState& State : OutStates)
		{
			UE_LOG(LogLoreSourceControl, Verbose, TEXT("  state: '%s' working=%d lock=%d"),
				*State.DepotFilename, (int32)State.WorkingState, (int32)State.LockState);
		}

		// 4) History.
		if (bWithHistory && OutHistory != nullptr)
		{
			for (const FString& File : Command.Files)
			{
				if (!FLorePathUtils::IsUnderWorkspace(File, Command.WorkingCopyRoot))
				{
					continue;
				}
				const FString Rel = FLorePathUtils::ToWorkspaceRelativePath(File, Command.WorkingCopyRoot);
				if (Rel.IsEmpty())
				{
					continue;
				}

				TArray<FString> HistoryResults;
				int32 HistoryReturn = -1;
				TArray<FString> Params;
				Params.Add(FString::Printf(TEXT("\"%s\""), *Rel));
				LoreSourceControlUtils::RunCommand(TEXT("file history"), Params, TArray<FString>(), Context, HistoryResults, Command.ErrorMessages, HistoryReturn);

				TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>> History;
				LoreSourceControlUtils::ParseHistoryResults(HistoryResults, File, Rel, History);
				if (History.Num() > 0)
				{
					OutHistory->Add(FLorePathUtils::MakeCacheKey(File), MoveTemp(History));
				}
			}
		}

		return bStatusOk;
	}

	/** Merge worker history into the cached states (game thread). */
	void MergeHistoryIntoCache(const TMap<FString, TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>>>& InHistory)
	{
		if (InHistory.Num() == 0)
		{
			return;
		}
		FLoreSourceControlModule* Module = FLoreSourceControlModule::GetPtr();
		if (Module == nullptr)
		{
			return;
		}
		FLoreSourceControlProvider& Provider = Module->GetProvider();
		for (const auto& Pair : InHistory)
		{
			TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe> State = Provider.GetStateInternal(Pair.Key);
			State->History = Pair.Value;
		}
	}
}

// -------------------------------------------------------------------------- Connect

FName FLoreConnectWorker::GetName() const
{
	return "Connect";
}

bool FLoreConnectWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);

	if (Context.BinaryPath.IsEmpty())
	{
		InCommand.ErrorMessages.Add(TEXT("Could not locate the `lore` executable. Set its path in the Lore source control settings."));
		return false;
	}

	TArray<FString> Results;
	int32 ReturnCode = -1;
	const bool bOk = LoreSourceControlUtils::RunCommand(TEXT("status"), TArray<FString>(), TArray<FString>(), Context, Results, InCommand.ErrorMessages, ReturnCode);

	if (!bOk)
	{
		InCommand.ErrorMessages.Add(TEXT("`lore status` failed: the working copy may not be a Lore repository, or the server is unreachable."));
	}
	else
	{
		InCommand.InfoMessages.Add(TEXT("Connected to Lore repository."));
	}

	InCommand.ReturnCode = ReturnCode;
	return bOk;
}

bool FLoreConnectWorker::UpdateStates() const
{
	return false;
}

// --------------------------------------------------------------------- UpdateStatus

FName FLoreUpdateStatusWorker::GetName() const
{
	return "UpdateStatus";
}

bool FLoreUpdateStatusWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	bool bWithHistory = false;
	if (InCommand.Operation->GetName() == "UpdateStatus")
	{
		const TSharedRef<FUpdateStatus, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FUpdateStatus>(InCommand.Operation);
		bWithHistory = Operation->ShouldUpdateHistory();
	}

	return RefreshFileStates(InCommand, /*bScan=*/true, bWithHistory, OutStates, &OutHistory);
}

bool FLoreUpdateStatusWorker::UpdateStates() const
{
	const bool bUpdated = LoreSourceControlUtils::UpdateCachedStates(OutStates);
	MergeHistoryIntoCache(OutHistory);
	return bUpdated || OutHistory.Num() > 0;
}

// ------------------------------------------------------------------------- CheckOut

FName FLoreCheckOutWorker::GetName() const
{
	return "CheckOut";
}

bool FLoreCheckOutWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);
	const TArray<FString> RelativeFiles = ToRelativePaths(InCommand.Files, InCommand.WorkingCopyRoot);

	TArray<FString> Results;
	const bool bOk = LoreSourceControlUtils::RunCommandBatched(
		TEXT("lock acquire"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/false,
		Results, InCommand.ErrorMessages);

	if (!bOk)
	{
		InCommand.ErrorMessages.Add(TEXT("Failed to acquire one or more locks (a file may be locked by another user)."));
	}

	RefreshFileStates(InCommand, /*bScan=*/false, /*bWithHistory=*/false, OutStates, nullptr);
	return bOk;
}

bool FLoreCheckOutWorker::UpdateStates() const
{
	return LoreSourceControlUtils::UpdateCachedStates(OutStates);
}

// ----------------------------------------------------------------------- MarkForAdd

FName FLoreMarkForAddWorker::GetName() const
{
	return "MarkForAdd";
}

bool FLoreMarkForAddWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);
	const TArray<FString> RelativeFiles = ToRelativePaths(InCommand.Files, InCommand.WorkingCopyRoot);

	TArray<FString> Results;
	const bool bOk = LoreSourceControlUtils::RunCommandBatched(
		TEXT("stage"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/true,
		Results, InCommand.ErrorMessages);

	RefreshFileStates(InCommand, /*bScan=*/true, /*bWithHistory=*/false, OutStates, nullptr);
	return bOk;
}

bool FLoreMarkForAddWorker::UpdateStates() const
{
	return LoreSourceControlUtils::UpdateCachedStates(OutStates);
}

// --------------------------------------------------------------------------- Delete

FName FLoreDeleteWorker::GetName() const
{
	return "Delete";
}

bool FLoreDeleteWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);

	// Remove the files on disk, then stage so Lore records the deletion.
	for (const FString& File : InCommand.Files)
	{
		if (FPaths::FileExists(File))
		{
			IFileManager::Get().Delete(*File, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
		}
	}

	const TArray<FString> RelativeFiles = ToRelativePaths(InCommand.Files, InCommand.WorkingCopyRoot);
	TArray<FString> Results;
	const bool bOk = LoreSourceControlUtils::RunCommandBatched(
		TEXT("stage"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/true,
		Results, InCommand.ErrorMessages);

	RefreshFileStates(InCommand, /*bScan=*/true, /*bWithHistory=*/false, OutStates, nullptr);
	return bOk;
}

bool FLoreDeleteWorker::UpdateStates() const
{
	return LoreSourceControlUtils::UpdateCachedStates(OutStates);
}

// --------------------------------------------------------------------------- Revert

FName FLoreRevertWorker::GetName() const
{
	return "Revert";
}

bool FLoreRevertWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);
	const TArray<FString> RelativeFiles = ToRelativePaths(InCommand.Files, InCommand.WorkingCopyRoot);

	// Discard local changes (best effort). Reverting an unmodified/unstaged file
	// is a legitimate no-op in the editor (e.g. "Revert Unchanged"), but `lore
	// reset` errors when there is nothing staged for a path, so we do not treat a
	// reset failure as a hard error: the post-op status refresh reflects reality.
	TArray<FString> ResetResults;
	LoreSourceControlUtils::RunCommandBatched(
		TEXT("reset"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/true,
		ResetResults, InCommand.ErrorMessages);

	// Release any locks we hold (best effort; not having a lock is not an error).
	TArray<FString> ReleaseResults;
	LoreSourceControlUtils::RunCommandBatched(
		TEXT("lock release"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/false,
		ReleaseResults, InCommand.ErrorMessages);

	RefreshFileStates(InCommand, /*bScan=*/true, /*bWithHistory=*/false, OutStates, nullptr);
	return true;
}

bool FLoreRevertWorker::UpdateStates() const
{
	return LoreSourceControlUtils::UpdateCachedStates(OutStates);
}

// ----------------------------------------------------------------------------- Sync

FName FLoreSyncWorker::GetName() const
{
	return "Sync";
}

bool FLoreSyncWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);

	TArray<FString> Results;
	int32 ReturnCode = -1;
	const bool bOk = LoreSourceControlUtils::RunCommand(TEXT("sync"), TArray<FString>(), TArray<FString>(), Context, Results, InCommand.ErrorMessages, ReturnCode);

	RefreshFileStates(InCommand, /*bScan=*/true, /*bWithHistory=*/false, OutStates, nullptr);
	return bOk;
}

bool FLoreSyncWorker::UpdateStates() const
{
	return LoreSourceControlUtils::UpdateCachedStates(OutStates);
}

// -------------------------------------------------------------------------- CheckIn

FName FLoreCheckInWorker::GetName() const
{
	return "CheckIn";
}

bool FLoreCheckInWorker::Execute(FLoreSourceControlCommand& InCommand)
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::MakeContext(InCommand);

	// Fail-closed pre-flight: refresh status/locks and verify every file is
	// submittable (not foreign-locked, not conflicted, at head) before mutating.
	{
		TArray<FLoreSourceControlState> PreflightStates;
		RefreshFileStates(InCommand, /*bScan=*/true, /*bWithHistory=*/false, PreflightStates, nullptr);

		TMap<FString, const FLoreSourceControlState*> ByKey;
		for (const FLoreSourceControlState& State : PreflightStates)
		{
			ByKey.Add(FLorePathUtils::MakeCacheKey(State.LocalFilename), &State);
		}

		for (const FString& File : InCommand.Files)
		{
			const FLoreSourceControlState* const* Found = ByKey.Find(FLorePathUtils::MakeCacheKey(File));
			if (Found == nullptr)
			{
				continue; // Unchanged files are skipped by stage/commit.
			}
			const FLoreSourceControlState* State = *Found;

			if (State->LockState == ELoreLockState::LockedByOther)
			{
				InCommand.ErrorMessages.Add(FString::Printf(TEXT("Cannot submit '%s': locked by %s."), *State->DepotFilename, *State->LockUser));
				return false;
			}
			if (State->WorkingState == ELoreWorkingState::Conflicted)
			{
				InCommand.ErrorMessages.Add(FString::Printf(TEXT("Cannot submit '%s': the file is in conflict. Resolve it first."), *State->DepotFilename));
				return false;
			}
			if (State->bNewerVersionOnServer)
			{
				InCommand.ErrorMessages.Add(FString::Printf(TEXT("Cannot submit '%s': the repository has newer changes. Sync first."), *State->DepotFilename));
				return false;
			}
		}
	}

	const TArray<FString> RelativeFiles = ToRelativePaths(InCommand.Files, InCommand.WorkingCopyRoot);

	// Stage.
	{
		TArray<FString> Results;
		if (!LoreSourceControlUtils::RunCommandBatched(TEXT("stage"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/true, Results, InCommand.ErrorMessages))
		{
			InCommand.ErrorMessages.Add(TEXT("Failed to stage changes for commit."));
			return false;
		}
	}

	// Commit.
	FString Description;
	if (InCommand.Operation->GetName() == "CheckIn")
	{
		const TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);
		Description = Operation->GetDescription().ToString();
	}
	if (Description.IsEmpty())
	{
		Description = TEXT("Submitted from Unreal Editor");
	}
	// Escape embedded quotes so the message survives the command line.
	Description.ReplaceInline(TEXT("\""), TEXT("\\\""));

	{
		TArray<FString> Results;
		int32 ReturnCode = -1;
		TArray<FString> Params;
		Params.Add(FString::Printf(TEXT("\"%s\""), *Description));
		if (!LoreSourceControlUtils::RunCommand(TEXT("commit"), Params, TArray<FString>(), Context, Results, InCommand.ErrorMessages, ReturnCode))
		{
			InCommand.ErrorMessages.Add(TEXT("`lore commit` failed."));
			return false;
		}
	}

	// Push.
	{
		TArray<FString> Results;
		int32 ReturnCode = -1;
		if (!LoreSourceControlUtils::RunCommand(TEXT("push"), TArray<FString>(), TArray<FString>(), Context, Results, InCommand.ErrorMessages, ReturnCode))
		{
			InCommand.ErrorMessages.Add(TEXT("`lore push` failed (the remote branch may have moved; sync and retry)."));
			return false;
		}
	}

	// Release locks for the submitted files now they are at head.
	{
		TArray<FString> Results;
		LoreSourceControlUtils::RunCommandBatched(TEXT("lock release"), TArray<FString>(), RelativeFiles, Context, /*bUseTargetsFile=*/false, Results, InCommand.ErrorMessages);
	}

	if (InCommand.Operation->GetName() == "CheckIn")
	{
		const TSharedRef<FCheckIn, ESPMode::ThreadSafe> Operation = StaticCastSharedRef<FCheckIn>(InCommand.Operation);
		Operation->SetSuccessMessage(LOCTEXT("CheckInSuccess", "Submitted revision to Lore."));
	}

	RefreshFileStates(InCommand, /*bScan=*/true, /*bWithHistory=*/false, OutStates, nullptr);
	return true;
}

bool FLoreCheckInWorker::UpdateStates() const
{
	return LoreSourceControlUtils::UpdateCachedStates(OutStates);
}

#undef LOCTEXT_NAMESPACE
