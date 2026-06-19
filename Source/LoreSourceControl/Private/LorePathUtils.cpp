#include "LorePathUtils.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace LorePathUtilsPrivate
{
bool LooksLikeEngineBinariesMisresolve(const FString& AbsolutePath)
{
	return AbsolutePath.Contains(TEXT("/Engine/Binaries/"), ESearchCase::IgnoreCase)
		|| AbsolutePath.Contains(TEXT("\\Engine\\Binaries\\"), ESearchCase::IgnoreCase);
}

bool LooksLikeMisresolvedEngineContentPath(const FString& AbsolutePath)
{
	return LooksLikeEngineBinariesMisresolve(AbsolutePath)
		&& (AbsolutePath.Contains(TEXT("/Content/"), ESearchCase::IgnoreCase)
			|| AbsolutePath.Contains(TEXT("\\Content\\"), ESearchCase::IgnoreCase));
}

FString TryRemapMisresolvedEnginePath(const FString& AbsolutePath)
{
	if (!LooksLikeMisresolvedEngineContentPath(AbsolutePath))
	{
		return FString();
	}

	const FString FileName = FPaths::GetCleanFilename(AbsolutePath);
	if (FileName.IsEmpty())
	{
		return FString();
	}

	const FString ContentCandidate = FLorePathUtils::NormalizeAbsolutePath(FPaths::Combine(FPaths::ProjectContentDir(), FileName));
	if (FPaths::FileExists(ContentCandidate))
	{
		return ContentCandidate;
	}

	return FString();
}
}

FString FLorePathUtils::NormalizeAbsolutePath(const FString& InPath)
{
	if (InPath.IsEmpty())
	{
		return FString();
	}

	FString Result = InPath;
	FPaths::NormalizeFilename(Result);
	Result = FPaths::ConvertRelativePathToFull(Result);
	FPaths::NormalizeFilename(Result);
	FPaths::CollapseRelativeDirectories(Result);
	FPaths::RemoveDuplicateSlashes(Result);
	return Result;
}

FString FLorePathUtils::NormalizeSourceControlPath(const FString& InPath, const FString& WorkspaceRoot)
{
	if (InPath.IsEmpty())
	{
		return FString();
	}

	FString Result = InPath;
	FPaths::NormalizeFilename(Result);

	if (FPaths::IsRelative(Result))
	{
		const FString Root = !WorkspaceRoot.IsEmpty()
			? NormalizeAbsolutePath(WorkspaceRoot)
			: NormalizeAbsolutePath(FPaths::ProjectDir());
		Result = NormalizeAbsolutePath(FPaths::Combine(Root, Result));
	}
	else
	{
		Result = NormalizeAbsolutePath(Result);
	}

	FPaths::CollapseRelativeDirectories(Result);
	FPaths::RemoveDuplicateSlashes(Result);
	return Result;
}

FString FLorePathUtils::ResolveSourceControlPath(const FString& InPath, const FString& WorkspaceRoot)
{
	if (InPath.IsEmpty())
	{
		return FString();
	}

	FString Candidate = InPath;
	FPaths::NormalizeFilename(Candidate);

	if (Candidate.Contains(TEXT("'")))
	{
		Candidate = FPackageName::ExportTextPathToObjectPath(Candidate);
	}

	if (Candidate.StartsWith(TEXT("/")) && FPackageName::IsValidLongPackageName(Candidate, false))
	{
		FString DiskPath;
		if (FPackageName::DoesPackageExist(Candidate, &DiskPath))
		{
			return NormalizeAbsolutePath(DiskPath);
		}

		FString PackageRelativePath;
		if (FPackageName::TryConvertLongPackageNameToFilename(Candidate, PackageRelativePath, FPackageName::GetAssetPackageExtension()))
		{
			return NormalizeSourceControlPath(PackageRelativePath, WorkspaceRoot);
		}
	}

	FString Resolved = NormalizeSourceControlPath(Candidate, WorkspaceRoot);
	if (const FString Remapped = LorePathUtilsPrivate::TryRemapMisresolvedEnginePath(Resolved); !Remapped.IsEmpty())
	{
		return Remapped;
	}

	if (const FString RemappedCandidate = LorePathUtilsPrivate::TryRemapMisresolvedEnginePath(NormalizeAbsolutePath(Candidate)); !RemappedCandidate.IsEmpty())
	{
		return RemappedCandidate;
	}

	return Resolved;
}

FString FLorePathUtils::ToWorkspaceRelativePath(const FString& AbsolutePath, const FString& WorkspaceRoot)
{
	FString Abs = ResolveSourceControlPath(AbsolutePath, WorkspaceRoot);
	FString Root = NormalizeAbsolutePath(WorkspaceRoot);

	if (!Root.EndsWith(TEXT("/")))
	{
		Root += TEXT("/");
	}

	FString Relative = Abs;
	if (FPaths::MakePathRelativeTo(Relative, *Root))
	{
		FPaths::NormalizeFilename(Relative);
		return Relative;
	}

	return Abs;
}

FString FLorePathUtils::ToAbsolutePath(const FString& WorkspaceRelativePath, const FString& WorkspaceRoot)
{
	if (FPaths::IsRelative(WorkspaceRelativePath))
	{
		return NormalizeAbsolutePath(FPaths::Combine(WorkspaceRoot, WorkspaceRelativePath));
	}

	return NormalizeAbsolutePath(WorkspaceRelativePath);
}

TArray<FString> FLorePathUtils::NormalizeFiles(const TArray<FString>& InFiles)
{
	return NormalizeFiles(InFiles, FString());
}

TArray<FString> FLorePathUtils::NormalizeFiles(const TArray<FString>& InFiles, const FString& WorkspaceRoot)
{
	TArray<FString> OutFiles;
	OutFiles.Reserve(InFiles.Num());

	for (const FString& File : InFiles)
	{
		if (!File.IsEmpty())
		{
			OutFiles.AddUnique(ResolveSourceControlPath(File, WorkspaceRoot));
		}
	}

	return OutFiles;
}

bool FLorePathUtils::IsUnderWorkspace(const FString& AbsolutePath, const FString& WorkspaceRoot)
{
	if (WorkspaceRoot.IsEmpty())
	{
		return false;
	}

	FString Abs = ResolveSourceControlPath(AbsolutePath, WorkspaceRoot);
	FString Root = NormalizeAbsolutePath(WorkspaceRoot);

#if PLATFORM_WINDOWS
	Abs = Abs.ToLower();
	Root = Root.ToLower();
#endif

	if (!Root.EndsWith(TEXT("/")))
	{
		Root += TEXT("/");
	}

	return Abs.StartsWith(Root) || Abs.Equals(Root.LeftChop(1));
}

FString FLorePathUtils::MakeCacheKey(const FString& InPath)
{
	FString Key = NormalizeAbsolutePath(InPath);
#if PLATFORM_WINDOWS
	Key = Key.ToLower();
#endif
	return Key;
}
