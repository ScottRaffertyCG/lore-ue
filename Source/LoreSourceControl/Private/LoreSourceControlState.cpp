#include "LoreSourceControlState.h"
#include "LoreSourceControlRevision.h"

#if SOURCE_CONTROL_WITH_SLATE
#include "Styling/AppStyle.h"
#include "RevisionControlStyle/RevisionControlStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LoreSourceControl.State"

int32 FLoreSourceControlState::GetHistorySize() const
{
	return History.Num();
}

TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FLoreSourceControlState::GetHistoryItem(int32 HistoryIndex) const
{
	if (History.IsValidIndex(HistoryIndex))
	{
		return History[HistoryIndex];
	}
	return nullptr;
}

TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FLoreSourceControlState::FindHistoryRevision(int32 RevisionNumber) const
{
	for (const auto& Revision : History)
	{
		if (Revision->GetRevisionNumber() == RevisionNumber)
		{
			return Revision;
		}
	}
	return nullptr;
}

TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FLoreSourceControlState::FindHistoryRevision(const FString& InRevision) const
{
	for (const auto& Revision : History)
	{
		if (Revision->GetRevision() == InRevision)
		{
			return Revision;
		}
	}
	return nullptr;
}

TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FLoreSourceControlState::GetCurrentRevision() const
{
	return nullptr;
}

ISourceControlState::FResolveInfo FLoreSourceControlState::GetResolveInfo() const
{
	return PendingResolveInfo;
}

#if SOURCE_CONTROL_WITH_SLATE
FSlateIcon FLoreSourceControlState::GetIcon() const
{
	switch (WorkingState)
	{
	case ELoreWorkingState::Modified:
	case ELoreWorkingState::Moved:
	case ELoreWorkingState::Copied:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.CheckedOut");
	case ELoreWorkingState::Added:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.OpenForAdd");
	case ELoreWorkingState::Deleted:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.MarkedForDelete");
	case ELoreWorkingState::Conflicted:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.Conflicted");
	case ELoreWorkingState::NotControlled:
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.NotInDepot");
	default:
		break;
	}

	if (LockState == ELoreLockState::LockedByOther)
	{
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.CheckedOutByOtherUser");
	}
	if (LockState == ELoreLockState::LockedByMe)
	{
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.CheckedOut");
	}
	if (bNewerVersionOnServer)
	{
		return FSlateIcon(FRevisionControlStyleManager::GetStyleSetName(), "RevisionControl.NotAtHeadRevision");
	}

	return FSlateIcon();
}
#endif

FText FLoreSourceControlState::GetDisplayName() const
{
	if (LockState == ELoreLockState::LockedByOther)
	{
		return FText::Format(LOCTEXT("LockedByOther", "Locked by {0}"), FText::FromString(LockUser));
	}

	switch (WorkingState)
	{
	case ELoreWorkingState::Unknown:
		return LOCTEXT("Unknown", "Unknown");
	case ELoreWorkingState::Clean:
		return LOCTEXT("Clean", "Up to date");
	case ELoreWorkingState::Added:
		return LOCTEXT("Added", "Marked for add");
	case ELoreWorkingState::Modified:
		return LOCTEXT("Modified", "Modified");
	case ELoreWorkingState::Deleted:
		return LOCTEXT("Deleted", "Marked for delete");
	case ELoreWorkingState::Moved:
		return LOCTEXT("Moved", "Moved");
	case ELoreWorkingState::Copied:
		return LOCTEXT("Copied", "Copied");
	case ELoreWorkingState::Conflicted:
		return LOCTEXT("Conflicted", "Conflicted");
	case ELoreWorkingState::NotControlled:
		return LOCTEXT("NotControlled", "Not under source control");
	case ELoreWorkingState::Ignored:
		return LOCTEXT("Ignored", "Ignored");
	case ELoreWorkingState::Missing:
		return LOCTEXT("Missing", "Missing");
	default:
		return FText::GetEmpty();
	}
}

FText FLoreSourceControlState::GetDisplayTooltip() const
{
	if (LockState == ELoreLockState::LockedByOther)
	{
		return FText::Format(LOCTEXT("LockedByOtherTooltip", "Locked for editing by: {0}"), FText::FromString(LockUser));
	}
	if (bNewerVersionOnServer)
	{
		return LOCTEXT("NotCurrentTooltip", "The file is not at the latest revision in the repository");
	}
	return GetDisplayName();
}

const FString& FLoreSourceControlState::GetFilename() const
{
	return LocalFilename;
}

const FDateTime& FLoreSourceControlState::GetTimeStamp() const
{
	return TimeStamp;
}

// Action availability truth table (Working x Lock), with bNewerVersionOnServer=false:
//
//   Working \ Lock   | NotLocked         | LockedByMe        | LockedByOther
//   -----------------+-------------------+-------------------+------------------
//   Clean            | (read-only)       | CheckIn-, Revert+ | (foreign lock)
//   Modified         | CheckIn+, CO+,    | CheckIn+, Revert+ | blocked (CheckIn-)
//                    | Revert+           |                   |
//   Added            | CheckIn+, Revert+ | CheckIn+, Revert+ | blocked
//   Deleted          | CheckIn+, Revert+ | CheckIn+, Revert+ | blocked
//   NotControlled    | Add+              | -                 | -
//
// "CO" = Checkout (acquire a Lore lock). A locally modified file that is not yet
// locked can still be submitted (locks are advisory) and can also be checked out
// to acquire the lock. When bNewerVersionOnServer is true, CheckIn and Delete are
// blocked until the working copy is synced.
bool FLoreSourceControlState::CanCheckIn() const
{
	// Fail closed: only allow submit when this file is editable by us, at head,
	// and not blocked by a foreign lock or a conflict.
	const bool bHasPendingChange =
		WorkingState == ELoreWorkingState::Added ||
		WorkingState == ELoreWorkingState::Modified ||
		WorkingState == ELoreWorkingState::Deleted ||
		WorkingState == ELoreWorkingState::Moved ||
		WorkingState == ELoreWorkingState::Copied;

	return bHasPendingChange
		&& LockState != ELoreLockState::LockedByOther
		&& WorkingState != ELoreWorkingState::Conflicted
		&& !bNewerVersionOnServer;
}

bool FLoreSourceControlState::CanCheckout() const
{
	if (LockState == ELoreLockState::LockedByOther)
	{
		return false;
	}
	if (WorkingState == ELoreWorkingState::NotControlled ||
		WorkingState == ELoreWorkingState::Ignored ||
		WorkingState == ELoreWorkingState::Unknown)
	{
		return false;
	}
	// Already checked out by us means there is nothing further to lock.
	return LockState != ELoreLockState::LockedByMe;
}

bool FLoreSourceControlState::IsCheckedOut() const
{
	return LockState == ELoreLockState::LockedByMe;
}

bool FLoreSourceControlState::IsCheckedOutOther(FString* Who) const
{
	if (LockState == ELoreLockState::LockedByOther)
	{
		if (Who != nullptr)
		{
			*Who = LockUser;
		}
		return true;
	}
	return false;
}

bool FLoreSourceControlState::IsCurrent() const
{
	return !bNewerVersionOnServer;
}

bool FLoreSourceControlState::IsSourceControlled() const
{
	return WorkingState != ELoreWorkingState::NotControlled
		&& WorkingState != ELoreWorkingState::Ignored
		&& WorkingState != ELoreWorkingState::Unknown;
}

bool FLoreSourceControlState::IsAdded() const
{
	return WorkingState == ELoreWorkingState::Added;
}

bool FLoreSourceControlState::IsDeleted() const
{
	return WorkingState == ELoreWorkingState::Deleted;
}

bool FLoreSourceControlState::IsIgnored() const
{
	return WorkingState == ELoreWorkingState::Ignored;
}

bool FLoreSourceControlState::CanEdit() const
{
	// We can edit when we hold the lock, or when the file already has a pending
	// add/modify (locking binary assets is the recommended workflow but text
	// edits don't strictly require a lock).
	return LockState == ELoreLockState::LockedByMe
		|| WorkingState == ELoreWorkingState::Added
		|| WorkingState == ELoreWorkingState::Modified;
}

bool FLoreSourceControlState::IsUnknown() const
{
	return WorkingState == ELoreWorkingState::Unknown;
}

bool FLoreSourceControlState::IsModified() const
{
	return WorkingState == ELoreWorkingState::Modified
		|| WorkingState == ELoreWorkingState::Added
		|| WorkingState == ELoreWorkingState::Deleted
		|| WorkingState == ELoreWorkingState::Moved
		|| WorkingState == ELoreWorkingState::Copied
		|| WorkingState == ELoreWorkingState::Conflicted;
}

bool FLoreSourceControlState::CanAdd() const
{
	return WorkingState == ELoreWorkingState::NotControlled;
}

bool FLoreSourceControlState::CanDelete() const
{
	return IsSourceControlled() && IsCurrent();
}

bool FLoreSourceControlState::CanRevert() const
{
	return IsModified() || LockState == ELoreLockState::LockedByMe;
}

#undef LOCTEXT_NAMESPACE
