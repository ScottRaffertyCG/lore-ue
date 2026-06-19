#include "LoreSourceControlRevision.h"
#include "LoreSourceControlUtils.h"
#include "LoreSourceControlLog.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

bool FLoreSourceControlRevision::Get(FString& InOutFilename, EConcurrency::Type InConcurrency) const
{
	const LoreSourceControlUtils::FLoreCommandContext Context = LoreSourceControlUtils::GetCommandContextFromModule();
	if (Context.BinaryPath.IsEmpty() || RepoFilename.IsEmpty() || Revision.IsEmpty())
	{
		return false;
	}

	// Materialize to a caller-supplied path, or a generated temp file.
	if (InOutFilename.IsEmpty())
	{
		const FString Extension = FPaths::GetExtension(Filename, /*bIncludeDot=*/true);
		InOutFilename = FPaths::Combine(
			FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()),
			TEXT("Temp"),
			FString::Printf(TEXT("Lore-%s-%s%s"), *Revision.Left(12), *FGuid::NewGuid().ToString(EGuidFormats::Digits), *Extension));
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(InOutFilename), /*Tree=*/true);

	TArray<FString> Parameters;
	Parameters.Add(FString::Printf(TEXT("--revision \"%s\""), *Revision));
	Parameters.Add(FString::Printf(TEXT("--path \"%s\""), *RepoFilename));
	Parameters.Add(FString::Printf(TEXT("--output \"%s\""), *InOutFilename));

	TArray<FString> Results;
	TArray<FString> Errors;
	int32 ReturnCode = -1;
	const bool bSuccess = LoreSourceControlUtils::RunCommand(
		TEXT("file write"), Parameters, TArray<FString>(), Context, Results, Errors, ReturnCode);

	if (!bSuccess || ReturnCode != 0 || !FPaths::FileExists(InOutFilename))
	{
		UE_LOG(LogLoreSourceControl, Error, TEXT("Failed to materialize revision %s of %s"), *Revision, *RepoFilename);
		return false;
	}

	return true;
}

bool FLoreSourceControlRevision::GetAnnotated(TArray<FAnnotationLine>& OutLines) const
{
	// Lore does not expose line-level blame through the CLI yet.
	return false;
}

bool FLoreSourceControlRevision::GetAnnotated(FString& InOutFilename) const
{
	return false;
}

const FString& FLoreSourceControlRevision::GetFilename() const
{
	return Filename;
}

int32 FLoreSourceControlRevision::GetRevisionNumber() const
{
	return RevisionNumber;
}

const FString& FLoreSourceControlRevision::GetRevision() const
{
	return Revision;
}

const FString& FLoreSourceControlRevision::GetDescription() const
{
	return Description;
}

const FString& FLoreSourceControlRevision::GetUserName() const
{
	return UserName;
}

const FString& FLoreSourceControlRevision::GetClientSpec() const
{
	static const FString EmptyString;
	return EmptyString;
}

const FString& FLoreSourceControlRevision::GetAction() const
{
	return Action;
}

TSharedPtr<ISourceControlRevision, ESPMode::ThreadSafe> FLoreSourceControlRevision::GetBranchSource() const
{
	return BranchSource;
}

const FDateTime& FLoreSourceControlRevision::GetDate() const
{
	return Date;
}

int32 FLoreSourceControlRevision::GetCheckInIdentifier() const
{
	return RevisionNumber;
}

int32 FLoreSourceControlRevision::GetFileSize() const
{
	return FileSize;
}
