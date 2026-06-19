#pragma once

#include "CoreMinimal.h"
#include "LoreTypes.h"
#include "Templates/SharedPointer.h"

class FLoreSourceControlState;
class FLoreSourceControlRevision;
class FLoreSourceControlCommand;

namespace LoreSourceControlUtils
{
	/** Parameters needed to invoke the `lore` CLI off the game thread. */
	struct FLoreCommandContext
	{
		/** Absolute path to the `lore` executable. */
		FString BinaryPath;

		/** Working copy root; used as the process working directory and `--repository`. */
		FString WorkingCopyRoot;

		/** Optional identity passed via `--identity`. */
		FString UserName;

		/** Force `--offline`. */
		bool bOffline = false;
	};

	/**
	 * Resolve the `lore` executable: explicit settings override, then PATH, then
	 * a binary bundled alongside the plugin. Returns an empty string if none found.
	 */
	FString FindLoreBinaryPath(const FString& InSettingsOverride);

	/** Run `lore --version` to confirm the binary is usable. */
	bool CheckLoreAvailable(const FString& InBinaryPath, FString& OutVersion);

	/** Build a context from the active module/provider (game thread). */
	FLoreCommandContext GetCommandContextFromModule();

	/** Build a context from a snapshotted command (worker thread safe). */
	FLoreCommandContext MakeContext(const FLoreSourceControlCommand& InCommand);

	/**
	 * Run a single `lore` invocation.
	 * @param InCommand     The subcommand, possibly multi-word (e.g. "file write").
	 * @param InParameters  Extra flags/arguments, pre-quoted as needed.
	 * @param InFiles       Absolute file paths (quoted automatically).
	 */
	bool RunCommand(
		const FString& InCommand,
		const TArray<FString>& InParameters,
		const TArray<FString>& InFiles,
		const FLoreCommandContext& InContext,
		TArray<FString>& OutResults,
		TArray<FString>& OutErrorMessages,
		int32& OutReturnCode);

	/**
	 * Run a command across a batched file list (chunked, or via a temp
	 * `--targets` file when bUseTargetsFile is set). Aggregates results/errors.
	 */
	bool RunCommandBatched(
		const FString& InCommand,
		const TArray<FString>& InParameters,
		const TArray<FString>& InFiles,
		const FLoreCommandContext& InContext,
		bool bUseTargetsFile,
		TArray<FString>& OutResults,
		TArray<FString>& OutErrorMessages);

	/** Remove ANSI/VT escape sequences from a line. */
	FString StripAnsi(const FString& InLine);

	/** Parse `lore status [--scan]` output into a repo header and per-file states. */
	bool ParseStatusResults(
		const TArray<FString>& InResults,
		const FString& InWorkingCopyRoot,
		FLoreRepoStatus& OutRepoStatus,
		TArray<FLoreSourceControlState>& OutStates);

	/** Parse `lore lock query` output into lock records. */
	void ParseLockQueryResults(const TArray<FString>& InResults, TArray<FLoreLockInfo>& OutLocks);

	/** Parse `lore file history <path>` output into revisions (newest first). */
	void ParseHistoryResults(
		const TArray<FString>& InResults,
		const FString& InAbsoluteFilename,
		const FString& InRepoFilename,
		TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>>& OutHistory);

	/**
	 * Merge lock records onto states, classifying each as LockedByMe/LockedByOther
	 * relative to CurrentUser. Paths are matched workspace-relative.
	 */
	void ApplyLocksToStates(
		const TArray<FLoreLockInfo>& InLocks,
		const FString& InCurrentUser,
		const FString& InWorkingCopyRoot,
		TArray<FLoreSourceControlState>& InOutStates);

	/** Copy worker-produced states into the provider's cache (game thread only). */
	bool UpdateCachedStates(const TArray<FLoreSourceControlState>& InStates);
}
