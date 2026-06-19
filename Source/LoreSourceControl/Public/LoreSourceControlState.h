#pragma once

#include "CoreMinimal.h"
#include "ISourceControlState.h"
#include "LoreTypes.h"
#include "Misc/DateTime.h"

class FLoreSourceControlRevision;

/**
 * Source-control state for a single file, populated from the `lore` CLI.
 * The provider keeps one of these per file in its state cache.
 */
class FLoreSourceControlState : public ISourceControlState
{
public:
	explicit FLoreSourceControlState(const FString& InLocalFilename)
		: LocalFilename(InLocalFilename)
	{
	}

	FLoreSourceControlState() = default;
	FLoreSourceControlState(const FLoreSourceControlState& Other) = default;
	FLoreSourceControlState(FLoreSourceControlState&& Other) noexcept = default;
	FLoreSourceControlState& operator=(const FLoreSourceControlState& Other) = default;
	FLoreSourceControlState& operator=(FLoreSourceControlState&& Other) noexcept = default;

	/** ISourceControlState interface */
	virtual int32 GetHistorySize() const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> GetHistoryItem(int32 HistoryIndex) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FindHistoryRevision(int32 RevisionNumber) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> FindHistoryRevision(const FString& InRevision) const override;
	virtual TSharedPtr<class ISourceControlRevision, ESPMode::ThreadSafe> GetCurrentRevision() const override;
	virtual FResolveInfo GetResolveInfo() const override;
#if SOURCE_CONTROL_WITH_SLATE
	virtual FSlateIcon GetIcon() const override;
#endif
	virtual FText GetDisplayName() const override;
	virtual FText GetDisplayTooltip() const override;
	virtual const FString& GetFilename() const override;
	virtual const FDateTime& GetTimeStamp() const override;
	virtual bool CanCheckIn() const override;
	virtual bool CanCheckout() const override;
	virtual bool IsCheckedOut() const override;
	virtual bool IsCheckedOutOther(FString* Who = nullptr) const override;
	virtual bool IsCheckedOutInOtherBranch(const FString& CurrentBranch = FString()) const override { return false; }
	virtual bool IsModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override { return false; }
	virtual bool IsCheckedOutOrModifiedInOtherBranch(const FString& CurrentBranch = FString()) const override { return IsCheckedOutInOtherBranch(CurrentBranch) || IsModifiedInOtherBranch(CurrentBranch); }
	virtual TArray<FString> GetCheckedOutBranches() const override { return TArray<FString>(); }
	virtual FString GetOtherUserBranchCheckedOuts() const override { return FString(); }
	virtual bool GetOtherBranchHeadModification(FString& HeadBranchOut, FString& ActionOut, int32& HeadChangeListOut) const override { return false; }
	virtual bool IsCurrent() const override;
	virtual bool IsSourceControlled() const override;
	virtual bool IsAdded() const override;
	virtual bool IsDeleted() const override;
	virtual bool IsIgnored() const override;
	virtual bool CanEdit() const override;
	virtual bool IsUnknown() const override;
	virtual bool IsModified() const override;
	virtual bool CanAdd() const override;
	virtual bool CanDelete() const override;
	virtual bool CanRevert() const override;

public:
	/** History of the item, newest first. */
	TArray<TSharedRef<FLoreSourceControlRevision, ESPMode::ThreadSafe>> History;

	/** Absolute path on disk; the state-cache key. */
	FString LocalFilename;

	/** Workspace-relative path as Lore reports it. */
	FString DepotFilename;

	/** Working-copy state from `lore status`. */
	ELoreWorkingState::Type WorkingState = ELoreWorkingState::Unknown;

	/** Lock state from `lore lock query`. */
	ELoreLockState::Type LockState = ELoreLockState::Unknown;

	/** Owner of the lock, when locked by another user. */
	FString LockUser;

	/** Branch the lock was acquired on. */
	FString LockBranch;

	/** True when the remote branch holds a newer revision than the local one. */
	bool bNewerVersionOnServer = false;

	/** Local synced revision string and number. */
	FString LocalRevision;
	int32 LocalRevisionNumber = 0;

	/** Pending resolve info for conflicted files. */
	FResolveInfo PendingResolveInfo;

	/** Timestamp of the last status refresh. */
	FDateTime TimeStamp = FDateTime(0);
};
