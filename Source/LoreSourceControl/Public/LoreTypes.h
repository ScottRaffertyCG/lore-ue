#pragma once

#include "CoreMinimal.h"

/** Connection lifecycle exposed through the provider. */
enum class ELoreConnectionState : uint8
{
	Disconnected,
	Connected,
	Offline,
	Error
};

/**
 * Working-copy state of a file, derived from `lore status`.
 * Mirrors the distinctions Lore draws between tracked/untracked, the staged
 * action (add/modify/delete/move/copy), conflicts, and out-of-date files.
 */
namespace ELoreWorkingState
{
	enum Type
	{
		Unknown,
		Clean,
		Added,
		Modified,
		Deleted,
		Moved,
		Copied,
		Conflicted,
		NotControlled,
		Ignored,
		Missing,
	};
}

/** Lock state of a file, derived from `lore lock query`/`lore lock status`. */
namespace ELoreLockState
{
	enum Type
	{
		Unknown,
		NotLocked,
		LockedByMe,
		LockedByOther,
	};
}

/** Parsed lock record from `lore lock query`. */
struct FLoreLockInfo
{
	/** Workspace-relative path reported by Lore. */
	FString Path;

	/** Resolved owner (display name when available, otherwise the user id). */
	FString Owner;

	/** Branch the lock was acquired on. */
	FString Branch;

	/** When the lock was acquired (best-effort; may be unset). */
	FDateTime LockedAt = FDateTime(0);
};

/** Repository-level status parsed from the header of `lore status`. */
struct FLoreRepoStatus
{
	FString RepositoryId;
	FString BranchName;
	FString LocalRevision;
	int32 LocalRevisionNumber = 0;
	FString RemoteRevision;
	int32 RemoteRevisionNumber = 0;
	bool bRemoteBranchExists = false;
	bool bLocalAhead = false;
	bool bRemoteAhead = false;
	bool bInSyncWithRemote = false;
};

/** A single history entry parsed from `lore file history`. */
struct FLoreRevisionInfo
{
	FString File;
	FString Revision;
	int32 RevisionNumber = 0;
	FString Author;
	FString Description;
	FString Branch;
	FString Action = TEXT("edit");
	FDateTime Date = FDateTime(0);
	int32 FileSize = 0;
};
