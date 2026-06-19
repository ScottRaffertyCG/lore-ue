#include "SLoreSourceControlSettingsWidget.h"
#include "LoreSourceControlModule.h"
#include "LoreSourceControlProvider.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Internationalization/Internationalization.h"

#define LOCTEXT_NAMESPACE "SLoreSourceControlSettings"

namespace
{
	TSharedRef<SWidget> MakeLabeledRow(const FText& Label, TSharedRef<SWidget> Field, const FText& Tooltip)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(140.0f)
				[
					SNew(STextBlock)
					.Text(Label)
					.ToolTipText(Tooltip)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				Field
			];
	}
}

void SLoreSourceControlSettings::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeLabeledRow(
				LOCTEXT("BinaryPathLabel", "lore executable"),
				SNew(SEditableTextBox)
					.Text(this, &SLoreSourceControlSettings::GetBinaryPathText)
					.HintText(LOCTEXT("BinaryPathHint", "Leave empty to auto-detect on PATH"))
					.OnTextCommitted(this, &SLoreSourceControlSettings::OnBinaryPathCommitted),
				LOCTEXT("BinaryPathTooltip", "Full path to the `lore` executable. If empty, the plugin searches PATH and the bundled binaries."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeLabeledRow(
				LOCTEXT("RepositoryUrlLabel", "Repository URL"),
				SNew(SEditableTextBox)
					.Text(this, &SLoreSourceControlSettings::GetRepositoryUrlText)
					.HintText(LOCTEXT("RepositoryUrlHint", "lore://host/repo (informational)"))
					.OnTextCommitted(this, &SLoreSourceControlSettings::OnRepositoryUrlCommitted),
				LOCTEXT("RepositoryUrlTooltip", "Remote repository URL. The working copy's .lore config is authoritative; this is informational."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f)
		[
			MakeLabeledRow(
				LOCTEXT("UserNameLabel", "Identity"),
				SNew(SEditableTextBox)
					.Text(this, &SLoreSourceControlSettings::GetUserNameText)
					.HintText(LOCTEXT("UserNameHint", "Display name shown on your commits and locks"))
					.OnTextCommitted(this, &SLoreSourceControlSettings::OnUserNameCommitted),
				LOCTEXT("UserNameTooltip", "A display label attributed to your commits and locks (passed to `lore --identity`). This is NOT a login or password \u2014 server authentication, when a server requires it, is handled by the `lore` CLI itself."))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 8.0f, 2.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(this, &SLoreSourceControlSettings::GetDetectedBinaryText)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(2.0f, 8.0f, 2.0f, 2.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("Reconnect", "Reconnect"))
			.ToolTipText(LOCTEXT("ReconnectTooltip", "Re-resolve the lore binary and re-test the repository connection."))
			.OnClicked(this, &SLoreSourceControlSettings::OnReconnectClicked)
		]
	];
}

FText SLoreSourceControlSettings::GetBinaryPathText() const
{
	return FText::FromString(FLoreSourceControlModule::Get().AccessSettings().GetBinaryPath());
}

void SLoreSourceControlSettings::OnBinaryPathCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	FLoreSourceControlModule& Module = FLoreSourceControlModule::Get();
	Module.AccessSettings().SetBinaryPath(InText.ToString().TrimStartAndEnd());
	Module.SaveSettings();
}

FText SLoreSourceControlSettings::GetRepositoryUrlText() const
{
	return FText::FromString(FLoreSourceControlModule::Get().AccessSettings().GetRepositoryUrl());
}

void SLoreSourceControlSettings::OnRepositoryUrlCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	FLoreSourceControlModule& Module = FLoreSourceControlModule::Get();
	Module.AccessSettings().SetRepositoryUrl(InText.ToString().TrimStartAndEnd());
	Module.SaveSettings();
}

FText SLoreSourceControlSettings::GetUserNameText() const
{
	return FText::FromString(FLoreSourceControlModule::Get().AccessSettings().GetUserName());
}

void SLoreSourceControlSettings::OnUserNameCommitted(const FText& InText, ETextCommit::Type InCommitType)
{
	FLoreSourceControlModule& Module = FLoreSourceControlModule::Get();
	Module.AccessSettings().SetUserName(InText.ToString().TrimStartAndEnd());
	Module.SaveSettings();
}

FText SLoreSourceControlSettings::GetDetectedBinaryText() const
{
	const FString Binary = FLoreSourceControlModule::Get().GetProvider().GetLoreBinaryPath();
	if (Binary.IsEmpty())
	{
		return LOCTEXT("NoBinary", "lore executable: not found");
	}
	return FText::Format(LOCTEXT("FoundBinary", "lore executable: {0}"), FText::FromString(Binary));
}

FReply SLoreSourceControlSettings::OnReconnectClicked()
{
	FLoreSourceControlModule& Module = FLoreSourceControlModule::Get();
	Module.GetProvider().Init(/*bForceConnection=*/true);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
