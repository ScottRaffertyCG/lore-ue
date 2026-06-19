#include "LoreSourceControlUtils.h"
#include "LoreSourceControlState.h"
#include "LoreSourceControlRevision.h"
#include "LoreSourceControlCommand.h"
#include "LoreSourceControlModule.h"
#include "LoreSourceControlProvider.h"
#include "LoreSourceControlLog.h"
#include "LorePathUtils.h"

#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Interfaces/IPluginManager.h"

namespace LoreSourceControlUtils
{
namespace
{
	const TCHAR* GetExecutableName()
	{
#if PLATFORM_WINDOWS
		return TEXT("lore.exe");
#else
		return TEXT("lore");
#endif
	}

	/** Split a multi-line process output blob into individual trimmed lines. */
	void SplitIntoLines(const FString& InBlob, TArray<FString>& OutLines)
	{
		InBlob.ParseIntoArrayLines(OutLines, /*InCullEmpty=*/false);
	}

	/** Map a Lore short action code to a working state for a staged/unstaged change. */
	ELoreWorkingState::Type WorkingStateFromCode(const FString& Code)
	{
		if (Code == TEXT("A")) return ELoreWorkingState::Added;
		if (Code == TEXT("M")) return ELoreWorkingState::Modified;
		if (Code == TEXT("D")) return ELoreWorkingState::Deleted;
		if (Code == TEXT("V")) return ELoreWorkingState::Moved;
		if (Code == TEXT("C")) return ELoreWorkingState::Copied;
		return ELoreWorkingState::Unknown;
	}

	FString ActionStringFromCode(const FString& Code)
	{
		if (Code == TEXT("A")) return TEXT("add");
		if (Code == TEXT("M")) return TEXT("edit");
		if (Code == TEXT("D")) return TEXT("delete");
		if (Code == TEXT("V")) return TEXT("move");
		if (Code == TEXT("C")) return TEXT("copy");
		return TEXT("edit");
	}

	/** Parse a "<code> <path>" or "<code> <from> -> <to>" file line. */
	bool ParseFileLine(const FString& InLine, FString& OutCode, FString& OutPath)
	{
		FString Trimmed = InLine.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}

		int32 SpaceIdx = INDEX_NONE;
		if (!Trimmed.FindChar(TEXT(' '), SpaceIdx))
		{
			return false;
		}

		OutCode = Trimmed.Left(SpaceIdx);
		if (OutCode.Len() > 1)
		{
			return false; // Not a recognized single-letter action code.
		}

		FString Rest = Trimmed.Mid(SpaceIdx + 1).TrimStart();

		// Move/copy lines: "<from> -> <to>"; keep the destination path.
		int32 ArrowIdx = Rest.Find(TEXT(" -> "), ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (ArrowIdx != INDEX_NONE)
		{
			Rest = Rest.Mid(ArrowIdx + 4).TrimStartAndEnd();
		}

		// Directory entries carry a trailing slash; the provider only tracks files.
		if (Rest.EndsWith(TEXT("/")))
		{
			return false;
		}

		OutPath = Rest;
		return !OutPath.IsEmpty();
	}

	/**
	 * Transient/generated directories that the editor should never treat as
	 * source-controlled assets, even if a Lore working copy happens to track
	 * them. Matched as a whole path segment so nested copies (e.g.
	 * Plugins/Foo/Binaries) are excluded too.
	 */
	bool IsTransientRepoPath(const FString& InRelPath)
	{
		static const TCHAR* const TransientSegments[] = {
			TEXT("Saved"),
			TEXT("Intermediate"),
			TEXT("Binaries"),
			TEXT("DerivedDataCache"),
			TEXT(".git"),
			TEXT(".lore"),
			TEXT(".vs"),
			TEXT(".idea"),
		};

		TArray<FString> Segments;
		InRelPath.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/true);
		for (const FString& Segment : Segments)
		{
			for (const TCHAR* Transient : TransientSegments)
			{
				if (Segment.Equals(Transient, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool LineStartsWithKey(const FString& InTrimmed, const TCHAR* InKey, FString& OutValue)
	{
		// Key lines look like "Revision  : <value>"; match on the word before ':'.
		int32 ColonIdx = INDEX_NONE;
		if (!InTrimmed.FindChar(TEXT(':'), ColonIdx))
		{
			return false;
		}
		const FString KeyPart = InTrimmed.Left(ColonIdx).TrimStartAndEnd();
		if (!KeyPart.Equals(InKey, ESearchCase::CaseSensitive))
		{
			return false;
		}
		OutValue = InTrimmed.Mid(ColonIdx + 1).TrimStartAndEnd();
		return true;
	}
}

FString StripAnsi(const FString& InLine)
{
	// Remove CSI/OSC escape sequences (ESC [ ... letter) and bare ESC chars.
	FString Out;
	Out.Reserve(InLine.Len());

	const TCHAR* Ptr = *InLine;
	const TCHAR* End = Ptr + InLine.Len();
	while (Ptr < End)
	{
		if (*Ptr == TCHAR(0x1B)) // ESC
		{
			++Ptr;
			if (Ptr < End && *Ptr == TCHAR('['))
			{
				++Ptr;
				while (Ptr < End && !( (*Ptr >= TCHAR('A') && *Ptr <= TCHAR('Z')) || (*Ptr >= TCHAR('a') && *Ptr <= TCHAR('z')) ))
				{
					++Ptr;
				}
				if (Ptr < End)
				{
					++Ptr; // consume the final command letter
				}
			}
			else
			{
				// Skip until a non-control terminator for other escapes.
				while (Ptr < End && *Ptr != TCHAR(0x07) && *Ptr != TCHAR('\\'))
				{
					++Ptr;
				}
				if (Ptr < End)
				{
					++Ptr;
				}
			}
		}
		else
		{
			Out.AppendChar(*Ptr);
			++Ptr;
		}
	}
	return Out;
}

FString FindLoreBinaryPath(const FString& InSettingsOverride)
{
	// 1) Explicit override.
	if (!InSettingsOverride.IsEmpty() && FPaths::FileExists(InSettingsOverride))
	{
		return FPaths::ConvertRelativePathToFull(InSettingsOverride);
	}

	const FString ExeName = GetExecutableName();

	// 2) PATH search.
	FString PathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
	if (!PathEnv.IsEmpty())
	{
#if PLATFORM_WINDOWS
		const TCHAR Separator = TEXT(';');
#else
		const TCHAR Separator = TEXT(':');
#endif
		TArray<FString> Dirs;
		PathEnv.ParseIntoArray(Dirs, &Separator, /*InCullEmpty=*/true);
		for (const FString& Dir : Dirs)
		{
			const FString Candidate = FPaths::Combine(Dir, ExeName);
			if (FPaths::FileExists(Candidate))
			{
				return FPaths::ConvertRelativePathToFull(Candidate);
			}
		}
	}

	// 3) Binary bundled with the plugin.
	if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("LoreSourceControl")))
	{
		const FString Bundled = FPaths::Combine(
			Plugin->GetBaseDir(), TEXT("Binaries"), TEXT("ThirdParty"), TEXT("Lore"),
			FPlatformProcess::GetBinariesSubdirectory(), ExeName);
		if (FPaths::FileExists(Bundled))
		{
			return FPaths::ConvertRelativePathToFull(Bundled);
		}
	}

	return FString();
}

bool CheckLoreAvailable(const FString& InBinaryPath, FString& OutVersion)
{
	if (InBinaryPath.IsEmpty() || !FPaths::FileExists(InBinaryPath))
	{
		return false;
	}

	int32 ReturnCode = -1;
	FString StdOut;
	FString StdErr;
	const bool bLaunched = FPlatformProcess::ExecProcess(*InBinaryPath, TEXT("--version"), &ReturnCode, &StdOut, &StdErr);
	if (bLaunched && ReturnCode == 0)
	{
		OutVersion = StripAnsi(StdOut).TrimStartAndEnd();
		return true;
	}
	return false;
}

FLoreCommandContext GetCommandContextFromModule()
{
	FLoreCommandContext Context;
	if (FLoreSourceControlModule* Module = FLoreSourceControlModule::GetPtr())
	{
		const FLoreSourceControlProvider& Provider = Module->GetProvider();
		Context.BinaryPath = Provider.GetLoreBinaryPath();
		Context.WorkingCopyRoot = Provider.GetWorkingCopyRoot();
		Context.UserName = Provider.GetUserName();
	}
	return Context;
}

FLoreCommandContext MakeContext(const FLoreSourceControlCommand& InCommand)
{
	FLoreCommandContext Context;
	Context.BinaryPath = InCommand.LoreBinaryPath;
	Context.WorkingCopyRoot = InCommand.WorkingCopyRoot;
	Context.UserName = InCommand.UserName;
	return Context;
}

bool RunCommand(
	const FString& InCommand,
	const TArray<FString>& InParameters,
	const TArray<FString>& InFiles,
	const FLoreCommandContext& InContext,
	TArray<FString>& OutResults,
	TArray<FString>& OutErrorMessages,
	int32& OutReturnCode)
{
	OutReturnCode = -1;

	if (InContext.BinaryPath.IsEmpty())
	{
		OutErrorMessages.Add(TEXT("The `lore` executable could not be located."));
		return false;
	}

	FString FullCommand = InCommand;

	for (const FString& Param : InParameters)
	{
		FullCommand += TEXT(" ");
		FullCommand += Param;
	}

	for (const FString& File : InFiles)
	{
		FullCommand += FString::Printf(TEXT(" \"%s\""), *File);
	}

	// Global flags: never paginate, root at the working copy, pin identity.
	FullCommand += TEXT(" --no-pager");
	if (!InContext.WorkingCopyRoot.IsEmpty())
	{
		FullCommand += FString::Printf(TEXT(" --repository \"%s\""), *InContext.WorkingCopyRoot);
	}
	if (!InContext.UserName.IsEmpty())
	{
		FullCommand += FString::Printf(TEXT(" --identity \"%s\""), *InContext.UserName);
	}
	if (InContext.bOffline)
	{
		FullCommand += TEXT(" --offline");
	}

	UE_LOG(LogLoreSourceControl, Verbose, TEXT("Running: lore %s"), *FullCommand);

	FString StdOut;
	FString StdErr;
	const TCHAR* WorkingDir = InContext.WorkingCopyRoot.IsEmpty() ? nullptr : *InContext.WorkingCopyRoot;
	const bool bLaunched = FPlatformProcess::ExecProcess(*InContext.BinaryPath, *FullCommand, &OutReturnCode, &StdOut, &StdErr, WorkingDir);

	if (!bLaunched)
	{
		OutErrorMessages.Add(FString::Printf(TEXT("Failed to launch `lore` at %s"), *InContext.BinaryPath));
		return false;
	}

	{
		TArray<FString> OutLines;
		SplitIntoLines(StdOut, OutLines);
		for (FString& Line : OutLines)
		{
			OutResults.Add(StripAnsi(Line));
		}
	}
	{
		TArray<FString> ErrLines;
		SplitIntoLines(StdErr, ErrLines);
		for (FString& Line : ErrLines)
		{
			const FString Clean = StripAnsi(Line).TrimStartAndEnd();
			if (!Clean.IsEmpty())
			{
				OutErrorMessages.Add(Clean);
			}
		}
	}

	return OutReturnCode == 0;
}

bool RunCommandBatched(
	const FString& InCommand,
	const TArray<FString>& InParameters,
	const TArray<FString>& InFiles,
	const FLoreCommandContext& InContext,
	bool bUseTargetsFile,
	TArray<FString>& OutResults,
	TArray<FString>& OutErrorMessages)
{
	if (InFiles.Num() == 0)
	{
		int32 ReturnCode = -1;
		return RunCommand(InCommand, InParameters, InFiles, InContext, OutResults, OutErrorMessages, ReturnCode);
	}

	// For commands that accept a targets file (status/stage/reset), write all the
	// paths to a temp file in one pass to avoid command-line length limits.
	if (bUseTargetsFile && InFiles.Num() > 1)
	{
		const FString TargetsPath = FPaths::Combine(
			FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
			TEXT("Temp"),
			FString::Printf(TEXT("lore-targets-%s.txt"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(TargetsPath), /*Tree=*/true);

		const FString Contents = FString::Join(InFiles, TEXT("\n"));
		// Must be written WITHOUT a BOM: `lore --targets` reads the file as raw
		// UTF-8 and treats a leading BOM as part of the first path (-> "Ignoring
		// invalid path"). Worse, a UTF-16 file is unparseable and causes `lore` to
		// silently ignore --targets and scan the entire repository. Both manifest
		// as wrong file states (modified files reported clean, transient files
		// flooding the submit dialog), so pin the encoding explicitly.
		if (FFileHelper::SaveStringToFile(Contents, *TargetsPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			TArray<FString> Params = InParameters;
			Params.Add(FString::Printf(TEXT("--targets \"%s\""), *TargetsPath));

			int32 ReturnCode = -1;
			const bool bResult = RunCommand(InCommand, Params, TArray<FString>(), InContext, OutResults, OutErrorMessages, ReturnCode);
			IFileManager::Get().Delete(*TargetsPath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
			return bResult;
		}
		// Fall through to chunked invocation if the temp file could not be written.
	}

	// Chunked invocation (also used by commands that only take positional paths,
	// such as `lore lock ...`).
	const int32 BatchSize = 30;
	bool bAllSucceeded = true;
	for (int32 Start = 0; Start < InFiles.Num(); Start += BatchSize)
	{
		TArray<FString> Batch;
		for (int32 i = Start; i < FMath::Min(Start + BatchSize, InFiles.Num()); ++i)
		{
			Batch.Add(InFiles[i]);
		}

		int32 ReturnCode = -1;
		bAllSucceeded &= RunCommand(InCommand, InParameters, Batch, InContext, OutResults, OutErrorMessages, ReturnCode);
	}
	return bAllSucceeded;
}

bool ParseStatusResults(
	const TArray<FString>& InResults,
	const FString& InWorkingCopyRoot,
	FLoreRepoStatus& OutRepoStatus,
	TArray<FLoreSourceControlState>& OutStates)
{
	enum class ESection
	{
		None,
		Staged,
		Conflict,
		NotStaged,
		Untracked,
	};
	ESection Section = ESection::None;

	auto ParseRevisionPair = [](const FString& In, int32& OutNumber, FString& OutHash)
	{
		int32 ArrowIdx = In.Find(TEXT(" -> "), ESearchCase::CaseSensitive, ESearchDir::FromStart);
		if (ArrowIdx != INDEX_NONE)
		{
			OutNumber = FCString::Atoi(*In.Left(ArrowIdx).TrimStartAndEnd());
			OutHash = In.Mid(ArrowIdx + 4).TrimStartAndEnd();
		}
		else
		{
			OutHash = In.TrimStartAndEnd();
		}
	};

	for (const FString& Raw : InResults)
	{
		const FString Line = Raw.TrimEnd();
		const FString Trimmed = Line.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			continue;
		}

		// Repository header lines.
		if (Trimmed.StartsWith(TEXT("Repository ")))
		{
			OutRepoStatus.RepositoryId = Trimmed.Mid(11).TrimStartAndEnd();
			Section = ESection::None;
			continue;
		}
		if (Trimmed.StartsWith(TEXT("On branch ")))
		{
			FString Rest = Trimmed.Mid(10);
			int32 RevIdx = Rest.Find(TEXT(" revision "), ESearchCase::CaseSensitive, ESearchDir::FromStart);
			if (RevIdx != INDEX_NONE)
			{
				OutRepoStatus.BranchName = Rest.Left(RevIdx).TrimStartAndEnd();
				ParseRevisionPair(Rest.Mid(RevIdx + 10), OutRepoStatus.LocalRevisionNumber, OutRepoStatus.LocalRevision);
			}
			else
			{
				OutRepoStatus.BranchName = Rest.TrimStartAndEnd();
			}
			Section = ESection::None;
			continue;
		}
		if (Trimmed.StartsWith(TEXT("Remote revision ")))
		{
			ParseRevisionPair(Trimmed.Mid(16), OutRepoStatus.RemoteRevisionNumber, OutRepoStatus.RemoteRevision);
			OutRepoStatus.bRemoteBranchExists = true;
			continue;
		}
		if (Trimmed.Equals(TEXT("Local branch in sync with remote")))
		{
			OutRepoStatus.bInSyncWithRemote = true;
			OutRepoStatus.bRemoteBranchExists = true;
			continue;
		}
		if (Trimmed.Equals(TEXT("Local branch is ahead of remote")))
		{
			OutRepoStatus.bLocalAhead = true;
			OutRepoStatus.bRemoteBranchExists = true;
			continue;
		}
		if (Trimmed.Equals(TEXT("Local branch is behind remote")))
		{
			OutRepoStatus.bRemoteAhead = true;
			OutRepoStatus.bRemoteBranchExists = true;
			continue;
		}
		if (Trimmed.Equals(TEXT("Local branch has diverged, synchronize to merge")))
		{
			OutRepoStatus.bLocalAhead = true;
			OutRepoStatus.bRemoteAhead = true;
			OutRepoStatus.bRemoteBranchExists = true;
			continue;
		}

		// Section headers.
		if (Trimmed.StartsWith(TEXT("Changes staged for commit")))
		{
			Section = ESection::Staged;
			continue;
		}
		if (Trimmed.StartsWith(TEXT("Changes in conflict")))
		{
			Section = ESection::Conflict;
			continue;
		}
		if (Trimmed.StartsWith(TEXT("Changes not staged for commit")))
		{
			Section = ESection::NotStaged;
			continue;
		}
		if (Trimmed.StartsWith(TEXT("Untracked files")))
		{
			Section = ESection::Untracked;
			continue;
		}
		if (Trimmed.StartsWith(TEXT("Tracked changes")) ||
			Trimmed.StartsWith(TEXT("No tracked changes")) ||
			Trimmed.StartsWith(TEXT("Repository size")))
		{
			Section = ESection::None;
			continue;
		}

		if (Section == ESection::None)
		{
			continue;
		}

		FString Code;
		FString RelPath;
		if (!ParseFileLine(Trimmed, Code, RelPath))
		{
			continue;
		}

		// Transient/generated directories are never source-controlled assets from
		// the editor's perspective. Many Lore working copies were initialised with
		// these committed (Saved/, Intermediate/, Binaries/, caches), which would
		// otherwise flood status and the submit dialog with thousands of entries.
		if (IsTransientRepoPath(RelPath))
		{
			continue;
		}

		const FString AbsPath = FLorePathUtils::ToAbsolutePath(RelPath, InWorkingCopyRoot);

		FLoreSourceControlState State(AbsPath);
		State.DepotFilename = RelPath;
		State.LocalRevision = OutRepoStatus.LocalRevision;
		State.LocalRevisionNumber = OutRepoStatus.LocalRevisionNumber;
		State.bNewerVersionOnServer = OutRepoStatus.bRemoteAhead;
		State.TimeStamp = FDateTime::Now();

		switch (Section)
		{
		case ESection::Staged:
		case ESection::NotStaged:
			State.WorkingState = WorkingStateFromCode(Code);
			break;
		case ESection::Conflict:
			State.WorkingState = ELoreWorkingState::Conflicted;
			State.PendingResolveInfo.RemoteFile = RelPath;
			State.PendingResolveInfo.RemoteRevision = OutRepoStatus.RemoteRevision;
			State.PendingResolveInfo.BaseRevision = OutRepoStatus.LocalRevision;
			break;
		case ESection::Untracked:
			State.WorkingState = ELoreWorkingState::NotControlled;
			break;
		default:
			break;
		}

		if (State.WorkingState != ELoreWorkingState::Unknown)
		{
			OutStates.Add(MoveTemp(State));
		}
	}

	return true;
}

void ParseLockQueryResults(const TArray<FString>& InResults, TArray<FLoreLockInfo>& OutLocks)
{
	for (const FString& Raw : InResults)
	{
		const FString Trimmed = Raw.TrimStartAndEnd();
		if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("Locks found")) || Trimmed.StartsWith(TEXT("Files locked")))
		{
			continue;
		}

		// "<path> by <owner> on branch <branch>" (query) or
		// "<path> by <owner> on <timestamp>" (status).
		int32 OnBranchIdx = Trimmed.Find(TEXT(" on branch "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		int32 OnIdx = (OnBranchIdx != INDEX_NONE) ? OnBranchIdx : Trimmed.Find(TEXT(" on "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (OnIdx == INDEX_NONE)
		{
			continue;
		}

		FLoreLockInfo Lock;
		if (OnBranchIdx != INDEX_NONE)
		{
			Lock.Branch = Trimmed.Mid(OnBranchIdx + 11).TrimStartAndEnd();
		}

		const FString Left = Trimmed.Left(OnIdx);
		const int32 ByIdx = Left.Find(TEXT(" by "), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (ByIdx == INDEX_NONE)
		{
			continue;
		}

		Lock.Path = Left.Left(ByIdx).TrimStartAndEnd();
		Lock.Owner = Left.Mid(ByIdx + 4).TrimStartAndEnd();
		if (!Lock.Path.IsEmpty())
		{
			OutLocks.Add(MoveTemp(Lock));
		}
	}
}

void ParseHistoryResults(
	const TArray<FString>& InResults,
	const FString& InAbsoluteFilename,
	const FString& InRepoFilename,
	TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>>& OutHistory)
{
	TSharedPtr<FLoreSourceControlRevision, ESPMode::ThreadSafe> Current;

	auto FinalizeCurrent = [&]()
	{
		if (Current.IsValid() && (Current->RevisionNumber != 0 || !Current->Revision.IsEmpty()))
		{
			OutHistory.Add(Current.ToSharedRef());
		}
		Current.Reset();
	};

	for (const FString& Raw : InResults)
	{
		const FString Trimmed = Raw.TrimStartAndEnd();
		FString Value;

		if (LineStartsWithKey(Trimmed, TEXT("Revision"), Value))
		{
			if (Current.IsValid() && (Current->RevisionNumber != 0 || !Current->Revision.IsEmpty()))
			{
				FinalizeCurrent();
			}
			if (!Current.IsValid())
			{
				Current = MakeShared<FLoreSourceControlRevision, ESPMode::ThreadSafe>();
				Current->Filename = InAbsoluteFilename;
				Current->RepoFilename = InRepoFilename;
			}
			Current->RevisionNumber = FCString::Atoi(*Value);
			continue;
		}

		if (!Current.IsValid())
		{
			// Lines before the first "Revision :" (the action header) are skipped;
			// the action is inferred from the header token below if present.
			if (!Trimmed.IsEmpty())
			{
				const FString Code = Trimmed.Left(1);
				if (Code == TEXT("A") || Code == TEXT("M") || Code == TEXT("D") || Code == TEXT("V") || Code == TEXT("C"))
				{
					Current = MakeShared<FLoreSourceControlRevision, ESPMode::ThreadSafe>();
					Current->Filename = InAbsoluteFilename;
					Current->RepoFilename = InRepoFilename;
					Current->Action = ActionStringFromCode(Code);
				}
			}
			continue;
		}

		if (LineStartsWithKey(Trimmed, TEXT("Signature"), Value))
		{
			Current->Revision = Value;
		}
		else if (LineStartsWithKey(Trimmed, TEXT("Branch"), Value))
		{
			Current->Branch = Value;
		}
		else if (LineStartsWithKey(Trimmed, TEXT("Creator"), Value))
		{
			if (Current->UserName.IsEmpty())
			{
				Current->UserName = Value;
			}
		}
		else if (LineStartsWithKey(Trimmed, TEXT("Committer"), Value))
		{
			Current->UserName = Value;
		}
		else if (LineStartsWithKey(Trimmed, TEXT("Date"), Value))
		{
			FDateTime Parsed;
			if (FDateTime::ParseHttpDate(Value, Parsed) || FDateTime::Parse(Value, Parsed))
			{
				Current->Date = Parsed;
			}
		}
		else if (Raw.StartsWith(TEXT("    ")))
		{
			// Indented commit-message body line.
			if (!Current->Description.IsEmpty())
			{
				Current->Description += TEXT("\n");
			}
			Current->Description += Trimmed;
		}
	}

	FinalizeCurrent();
}

void ApplyLocksToStates(
	const TArray<FLoreLockInfo>& InLocks,
	const FString& InCurrentUser,
	const FString& InWorkingCopyRoot,
	TArray<FLoreSourceControlState>& InOutStates)
{
	if (InLocks.Num() == 0)
	{
		return;
	}

	// Build an absolute-path -> lock map.
	TMap<FString, const FLoreLockInfo*> LockByPath;
	for (const FLoreLockInfo& Lock : InLocks)
	{
		const FString Abs = FLorePathUtils::MakeCacheKey(FLorePathUtils::ToAbsolutePath(Lock.Path, InWorkingCopyRoot));
		LockByPath.Add(Abs, &Lock);
	}

	// A lock whose owner the server cannot attribute to a named user (e.g. an
	// unauthenticated dev server reports "<unknown>") is treated as ours: there
	// is no identity to distinguish holders, and the local client is the actor.
	// When the server authenticates, owners resolve to real names and the
	// identity comparison below applies normally.
	auto IsUnattributedOwner = [](const FString& Owner)
	{
		return Owner.IsEmpty()
			|| Owner.Equals(TEXT("<unknown>"), ESearchCase::IgnoreCase)
			|| Owner.Equals(TEXT("unknown"), ESearchCase::IgnoreCase);
	};

	for (FLoreSourceControlState& State : InOutStates)
	{
		const FString Key = FLorePathUtils::MakeCacheKey(State.LocalFilename);
		if (const FLoreLockInfo* const* Found = LockByPath.Find(Key))
		{
			const FLoreLockInfo* Lock = *Found;
			const bool bMine = IsUnattributedOwner(Lock->Owner)
				|| (!InCurrentUser.IsEmpty() && Lock->Owner.Equals(InCurrentUser, ESearchCase::IgnoreCase));
			State.LockState = bMine ? ELoreLockState::LockedByMe : ELoreLockState::LockedByOther;
			State.LockUser = Lock->Owner;
			State.LockBranch = Lock->Branch;
		}
		else if (State.LockState == ELoreLockState::Unknown)
		{
			State.LockState = ELoreLockState::NotLocked;
		}
	}
}

bool UpdateCachedStates(const TArray<FLoreSourceControlState>& InStates)
{
	check(IsInGameThread());
	FLoreSourceControlModule* Module = FLoreSourceControlModule::GetPtr();
	if (Module == nullptr)
	{
		return false;
	}

	FLoreSourceControlProvider& Provider = Module->GetProvider();
	for (const FLoreSourceControlState& InState : InStates)
	{
		TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe> CachedState = Provider.GetStateInternal(InState.LocalFilename);

		// Preserve history unless the incoming state supplies new history.
		TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>> PreservedHistory = CachedState->History;

		*CachedState = InState;
		if (InState.History.Num() == 0)
		{
			CachedState->History = MoveTemp(PreservedHistory);
		}
		CachedState->TimeStamp = FDateTime::Now();
	}

	return InStates.Num() > 0;
}

} // namespace LoreSourceControlUtils
