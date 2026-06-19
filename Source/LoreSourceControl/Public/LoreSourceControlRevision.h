#pragma once

#include "CoreMinimal.h"
#include "ISourceControlRevision.h"
#include "Misc/DateTime.h"

/** A single revision of a file in a Lore repository. */
class FLoreSourceControlRevision : public ISourceControlRevision
{
public:
	FLoreSourceControlRevision() = default;

	/** ISourceControlRevision interface */
	virtual bool Get(FString& InOutFilename, EConcurrency::Type InConcurrency = EConcurrency::Synchronous) const override;
	virtual bool GetAnnotated(TArray<FAnnotationLine>& OutLines) const override;
	virtual bool GetAnnotated(FString& InOutFilename) const override;
	virtual const FString& GetFilename() const override;
	virtual int32 GetRevisionNumber() const override;
	virtual const FString& GetRevision() const override;
	virtual const FString& GetDescription() const override;
	virtual const FString& GetUserName() const override;
	virtual const FString& GetClientSpec() const override;
	virtual const FString& GetAction() const override;
	virtual TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> GetBranchSource() const override;
	virtual const FDateTime& GetDate() const override;
	virtual int32 GetCheckInIdentifier() const override;
	virtual int32 GetFileSize() const override;

public:
	/** Absolute local filename this revision refers to. */
	FString Filename;

	/** Workspace-relative path used as the `--path` argument to `lore file write`. */
	FString RepoFilename;

	/** Revision number (sequential). */
	int32 RevisionNumber = 0;

	/** Revision signature/hash used as the `--revision` argument. */
	FString Revision;

	/** Commit message. */
	FString Description;

	/** Author. */
	FString UserName;

	/** Action performed (add/edit/delete/...). */
	FString Action;

	/** Branch this revision was made on. */
	FString Branch;

	/** Source of branch, if any. */
	TSharedPtr<FLoreSourceControlRevision, ESPMode::ThreadSafe> BranchSource;

	/** Date this revision was made. */
	FDateTime Date = FDateTime(0);

	/** File size in bytes (0 if unknown). */
	int32 FileSize = 0;
};
