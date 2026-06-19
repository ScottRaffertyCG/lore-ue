#include "LoreSourceControlSettings.h"
#include "SourceControlHelpers.h"

namespace
{
	const TCHAR* SettingsSection = TEXT("LoreSourceControl.LoreSourceControlSettings");
}

FString FLoreSourceControlSettings::GetBinaryPath() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return BinaryPath;
}

void FLoreSourceControlSettings::SetBinaryPath(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	BinaryPath = InString;
}

FString FLoreSourceControlSettings::GetRepositoryUrl() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return RepositoryUrl;
}

void FLoreSourceControlSettings::SetRepositoryUrl(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	RepositoryUrl = InString;
}

FString FLoreSourceControlSettings::GetUserName() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return UserName;
}

void FLoreSourceControlSettings::SetUserName(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	UserName = InString;
}

FString FLoreSourceControlSettings::GetEphemeralPassword() const
{
	FScopeLock ScopeLock(&CriticalSection);
	return EphemeralPassword;
}

void FLoreSourceControlSettings::SetEphemeralPassword(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);
	EphemeralPassword = InString;
}

void FLoreSourceControlSettings::LoadSettings()
{
	FScopeLock ScopeLock(&CriticalSection);
	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->GetString(SettingsSection, TEXT("BinaryPath"), BinaryPath, IniFile);
	GConfig->GetString(SettingsSection, TEXT("RepositoryUrl"), RepositoryUrl, IniFile);
	GConfig->GetString(SettingsSection, TEXT("UserName"), UserName, IniFile);
}

void FLoreSourceControlSettings::SaveSettings() const
{
	FScopeLock ScopeLock(&CriticalSection);
	const FString& IniFile = SourceControlHelpers::GetSettingsIni();
	GConfig->SetString(SettingsSection, TEXT("BinaryPath"), *BinaryPath, IniFile);
	GConfig->SetString(SettingsSection, TEXT("RepositoryUrl"), *RepositoryUrl, IniFile);
	GConfig->SetString(SettingsSection, TEXT("UserName"), *UserName, IniFile);
	// EphemeralPassword is intentionally not written.
}
