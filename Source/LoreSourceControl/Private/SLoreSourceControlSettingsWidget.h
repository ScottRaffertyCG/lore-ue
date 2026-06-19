#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SEditableTextBox;

/** Provider settings panel shown in Unreal's Revision Control preferences. */
class SLoreSourceControlSettings final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLoreSourceControlSettings) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Binary path.
	FText GetBinaryPathText() const;
	void OnBinaryPathCommitted(const FText& InText, ETextCommit::Type InCommitType);

	// Repository URL.
	FText GetRepositoryUrlText() const;
	void OnRepositoryUrlCommitted(const FText& InText, ETextCommit::Type InCommitType);

	// User identity.
	FText GetUserNameText() const;
	void OnUserNameCommitted(const FText& InText, ETextCommit::Type InCommitType);

	// Detected status.
	FText GetDetectedBinaryText() const;

	FReply OnReconnectClicked();
};
