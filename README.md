# Lore Source Control for Unreal Engine

**Native [Lore](https://github.com/EpicGames/lore) revision control inside the Unreal Editor.**

This plugin adds Lore as a source control provider in Unreal Engine, alongside the
engine's built-in Perforce, Git, and Subversion providers. It drives the public
`lore` command-line interface as a child process, so there is no native library to
build or link.

> **Note**
> This plugin is pre-1.0 and tracks Lore while Lore itself is pre-1.0. Behavior and
> on-disk formats may change between releases.

## Supported Engine Versions

| Unreal Engine | Status |
| --- | --- |
| 5.5 | Supported |
| 5.6 | Supported |
| 5.7 | Supported (primary development target) |
| 5.8 | Supported |

## Requirements

- Unreal Engine 5.5, 5.6, 5.7, or 5.8
- The [`lore` CLI](https://github.com/EpicGames/lore) installed and on your `PATH`
  (or point the plugin at it in settings)
- An existing Lore working copy for your project (run `lore` once to initialize or
  sync a repository in your project directory)

## Install

1. Copy or unzip the `LoreSourceControl` folder into your project's `Plugins` directory:

```text
<YourProject>/Plugins/LoreSourceControl/
```

2. Regenerate project files and build the Editor target.
3. Launch the editor, open **Revision Control > Change Source Control Settings**,
   and choose **Lore** as the provider.

If `lore` is not on your `PATH`, set the executable path in the Lore settings panel.

## Overview

- **Status overlays** — modified, added, deleted, and locked assets are surfaced in
  the Content Browser, including files changed locally without an explicit checkout.
- **Check out / check in** — acquire and release Lore locks, then stage, commit, and
  push from the editor's submit dialog.
- **Sync and history** — pull the latest revisions and view per-file history and
  diffs against the head revision.
- **Lock awareness** — files locked by other users are shown and protected from
  accidental submission.
- **Fail-closed submits** — check-in re-verifies lock ownership and head state before
  it mutates anything.

## Identity vs. authentication

The **Identity** field in the Lore settings panel is **not a login**. It is a display
label (passed to `lore --identity`) that is attributed to your commits and locks so
teammates can see who did what. It has no password and grants no access.

Server **authentication** — when a Lore server requires it — is handled entirely by
the `lore` CLI itself (for example via `lore login` or the server's configured
credential flow), not by this plugin. The plugin never stores or transmits passwords.

## How it works

Every operation maps to a `lore` CLI invocation. The plugin parses the CLI's
human-readable output to build Unreal's source control state, runs work on background
threads, and refreshes overlays on the game thread. Asset saves trigger a status
refresh so freshly edited files show the correct state without a manual refresh.

The CLI output format contract is documented in `Docs/LORE_CLI_OUTPUT_FORMATS.md`.

## Validation

Run the static layout check without a UE install:

```powershell
python Tools/validate_layout.py .
```

See `Docs/VALIDATION.md` for the full checklist including UE compile and automation
test steps.

## Building and packaging

See `Tools/stage_plugin.ps1` to stage the plugin into a local UE project, and
`Tools/package_plugin.ps1` to produce a release zip for a specific engine version.

## Known pre-1.0 limitations

- `lore file history` is fetched once per file on large Content Browser selections;
  batch history is not yet supported.
- `TryToDownloadFileFromBackgroundThread` is not yet implemented; diff materializes
  synchronously via `lore file write`.
- Cancellation of in-flight `lore` commands is not supported; operations run to
  completion or time out.
- Changelists, shelve/unshelve, and cross-branch state warnings are not implemented.

## License

Released under the MIT License. See [LICENSE](LICENSE).

Unreal Engine and Epic Games are trademarks or registered trademarks of Epic Games,
Inc. Lore is a project of Epic Games, Inc. This plugin is an independent integration
and references Unreal Engine public interfaces by include name only.
