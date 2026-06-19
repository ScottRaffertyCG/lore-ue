#pragma once

#include "CoreMinimal.h"
#include "ISourceControlProvider.h"
#include "LoreTypes.h"

class FLoreSourceControlState;
class FLoreSourceControlCommand;
class ILoreSourceControlWorker;
class UPackage;
class FObjectPostSaveContext;

typedef TSharedRef<ILoreSourceControlWorker, ESPMode::ThreadSafe> FLoreSourceControlWorkerRef;

DECLARE_DELEGATE_RetVal(FLoreSourceControlWorkerRef, FGetLoreSourceControlWorker)

class FLoreSourceControlProvider : public ISourceControlProvider
{
public:
	FLoreSourceControlProvider() = default;

	/* ISourceControlProvider implementation */
	virtual void Init(bool bForceConnection = true) override;
	virtual void Close() override;
	virtual FText GetStatusText() const override;
	virtual TMap<EStatus, FString> GetStatus() const override;
	virtual bool IsEnabled() const override;
	virtual bool IsAvailable() const override;
	virtual const FName& GetName() const override;
	virtual bool QueryStateBranchConfig(const FString& ConfigSrc, const FString& ConfigDest) override { return false; }
	virtual void RegisterStateBranches(const TArray<FString>& BranchNames, const FString& ContentRoot) override {}
	virtual int32 GetStateBranchIndex(const FString& BranchName) const override { return INDEX_NONE; }
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	virtual bool GetStateBranchAtIndex(int32 BranchIndex, FString& OutBranchName) const override { return false; }
#endif
	virtual ECommandResult::Type GetState(const TArray<FString>& InFiles, TArray<FSourceControlStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
	virtual ECommandResult::Type GetState(const TArray<FSourceControlChangelistRef>& InChangelists, TArray<FSourceControlChangelistStateRef>& OutState, EStateCacheUsage::Type InStateCacheUsage) override;
	virtual TArray<FSourceControlStateRef> GetCachedStateByPredicate(TFunctionRef<bool(const FSourceControlStateRef&)> Predicate) const override;
	virtual FDelegateHandle RegisterSourceControlStateChanged_Handle(const FSourceControlStateChanged::FDelegate& SourceControlStateChanged) override;
	virtual void UnregisterSourceControlStateChanged_Handle(FDelegateHandle Handle) override;
	virtual ECommandResult::Type Execute(const FSourceControlOperationRef& InOperation, FSourceControlChangelistPtr InChangelist, const TArray<FString>& InFiles, EConcurrency::Type InConcurrency = EConcurrency::Synchronous, const FSourceControlOperationComplete& InOperationCompleteDelegate = FSourceControlOperationComplete()) override;
	virtual bool CanExecuteOperation(const FSourceControlOperationRef& InOperation) const override;
	virtual bool CanCancelOperation(const FSourceControlOperationRef& InOperation) const override;
	virtual void CancelOperation(const FSourceControlOperationRef& InOperation) override;
	virtual bool UsesLocalReadOnlyState() const override;
	virtual bool UsesChangelists() const override;
	virtual bool UsesUncontrolledChangelists() const override;
	virtual bool UsesCheckout() const override;
	virtual bool UsesFileRevisions() const override;
	virtual bool UsesSnapshots() const override;
	virtual bool AllowsDiffAgainstDepot() const override;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 8
	virtual TOptional<bool> IsAtLatestRevision() const override;
	virtual TOptional<int> GetNumLocalChanges() const override;
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	virtual bool UsesSoftRevertOnDelete() const override { return false; }
	virtual TOptional<bool> HasChangesToSync() const override { return TOptional<bool>(); }
	virtual TOptional<bool> HasChangesToCheckIn() const override { return TOptional<bool>(); }
#endif
	virtual void Tick() override;
	virtual TArray<TSharedRef<class ISourceControlLabel>> GetLabels(const FString& InMatchingSpec) const override;
	virtual TArray<FSourceControlChangelistRef> GetChangelists(EStateCacheUsage::Type InStateCacheUsage) override;
#if SOURCE_CONTROL_WITH_SLATE
	virtual TSharedRef<class SWidget> MakeSettingsWidget() const override;
#endif

	using ISourceControlProvider::Execute;

	/** Resolved path to the `lore` executable. */
	const FString& GetLoreBinaryPath() const { return LoreBinaryPath; }

	/** Root of the working copy (project directory). */
	const FString& GetWorkingCopyRoot() const { return WorkingCopyRoot; }
	void SetWorkingCopyRoot(const FString& InRoot) { WorkingCopyRoot = InRoot; }

	/** Identity used for status/locks. */
	const FString& GetUserName() const { return UserName; }

	/** Get (creating if needed) the cached state for a file. */
	TSharedRef<FLoreSourceControlState, ESPMode::ThreadSafe> GetStateInternal(const FString& Filename);

	/** Remove a file from the state cache. */
	bool RemoveFileFromCache(const FString& Filename);

	/** Register a worker factory for an operation name. */
	void RegisterWorker(const FName& InName, const FGetLoreSourceControlWorker& InDelegate);

	/** Current connection state. */
	ELoreConnectionState GetConnectionState() const { return ConnectionState; }

private:
	TSharedPtr<class ILoreSourceControlWorker, ESPMode::ThreadSafe> CreateWorker(const FName& InOperationName) const;

	/** Resolve the working copy root and lore binary from settings. */
	void RefreshConnectionSettings();

	/** Run a command synchronously, pumping Tick() until it completes. */
	ECommandResult::Type ExecuteSynchronousCommand(FLoreSourceControlCommand& InCommand, const FText& Task);

	/** Queue a command for synchronous or asynchronous execution. */
	ECommandResult::Type IssueCommand(FLoreSourceControlCommand& InCommand, bool bSynchronous);

	/** Log a command's accumulated info/error messages. */
	void OutputCommandMessages(const FLoreSourceControlCommand& InCommand) const;

	/**
	 * Called by the editor after a package is written to disk. The editor only
	 * queries source-control status *before* a save (to confirm it can write),
	 * never after, so a freshly-modified asset would otherwise keep its stale
	 * "clean" overlay until the next manual status pass. We optimistically flag
	 * the file as modified and queue it for a confirming async status refresh.
	 */
	void HandlePackageSaved(const FString& InPackageFilename, UPackage* InPackage, FObjectPostSaveContext InObjectSaveContext);

private:
	FString WorkingCopyRoot;
	FString LoreBinaryPath;
	FString UserName;

	ELoreConnectionState ConnectionState = ELoreConnectionState::Disconnected;

	TMap<FString, TSharedRef<class FLoreSourceControlState, ESPMode::ThreadSafe>> StateCache;
	TMap<FName, FGetLoreSourceControlWorker> WorkersMap;
	TArray<FLoreSourceControlCommand*> CommandQueue;
	FSourceControlStateChanged OnSourceControlStateChanged;

	/** Absolute paths saved since the last Tick, awaiting a debounced status refresh. */
	TArray<FString> PackagesToRefresh;

	/** Handle for the PackageSavedWithContextEvent subscription. */
	FDelegateHandle OnPackageSavedHandle;
};
