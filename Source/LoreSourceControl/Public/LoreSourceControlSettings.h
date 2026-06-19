#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"

/**
 * Persistent settings for the Lore provider, stored in
 * Saved/Config/.../SourceControlSettings.ini under the [LoreSourceControl]
 * section. Credentials are deliberately NOT persisted; they live in memory for
 * the session only.
 */
class FLoreSourceControlSettings
{
public:
	/** Path to the `lore` executable (empty = auto-detect on PATH/bundled). */
	FString GetBinaryPath() const;
	void SetBinaryPath(const FString& InString);

	/** Remote repository URL (informational; the working copy's .lore config is authoritative). */
	FString GetRepositoryUrl() const;
	void SetRepositoryUrl(const FString& InString);

	/** Display identity / username used for status and locks. */
	FString GetUserName() const;
	void SetUserName(const FString& InString);

	/** Ephemeral, in-memory only credential (never written to disk). */
	FString GetEphemeralPassword() const;
	void SetEphemeralPassword(const FString& InString);

	/** Load/save settings from/to the source control settings INI. */
	void LoadSettings();
	void SaveSettings() const;

private:
	mutable FCriticalSection CriticalSection;

	FString BinaryPath;
	FString RepositoryUrl;
	FString UserName;

	/** Not persisted. */
	FString EphemeralPassword;
};
