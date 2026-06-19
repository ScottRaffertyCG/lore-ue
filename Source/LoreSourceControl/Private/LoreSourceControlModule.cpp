#include "LoreSourceControlModule.h"
#include "LoreSourceControlOperations.h"
#include "LoreSourceControlLog.h"

#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Misc/App.h"

#define LOCTEXT_NAMESPACE "LoreSourceControl"

template<typename Type>
static TSharedRef<ILoreSourceControlWorker, ESPMode::ThreadSafe> CreateWorker()
{
	return MakeShared<Type, ESPMode::ThreadSafe>();
}

void FLoreSourceControlModule::StartupModule()
{
	LoreSettings.LoadSettings();

	RegisterWorkers();

	// Bind our provider to the editor's modular source-control feature.
	IModularFeatures::Get().RegisterModularFeature("SourceControl", &LoreProvider);

	UE_LOG(LogLoreSourceControl, Log, TEXT("Lore source control module started."));
}

void FLoreSourceControlModule::ShutdownModule()
{
	LoreProvider.Close();

	IModularFeatures::Get().UnregisterModularFeature("SourceControl", &LoreProvider);

	UE_LOG(LogLoreSourceControl, Log, TEXT("Lore source control module shut down."));
}

void FLoreSourceControlModule::SaveSettings()
{
	if (FApp::IsUnattended() || IsRunningCommandlet())
	{
		return;
	}
	LoreSettings.SaveSettings();
}

void FLoreSourceControlModule::RegisterWorkers()
{
	LoreProvider.RegisterWorker("Connect", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreConnectWorker>));
	LoreProvider.RegisterWorker("UpdateStatus", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreUpdateStatusWorker>));
	LoreProvider.RegisterWorker("CheckOut", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreCheckOutWorker>));
	LoreProvider.RegisterWorker("CheckIn", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreCheckInWorker>));
	LoreProvider.RegisterWorker("MarkForAdd", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreMarkForAddWorker>));
	LoreProvider.RegisterWorker("Delete", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreDeleteWorker>));
	LoreProvider.RegisterWorker("Revert", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreRevertWorker>));
	LoreProvider.RegisterWorker("Sync", FGetLoreSourceControlWorker::CreateStatic(&CreateWorker<FLoreSyncWorker>));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FLoreSourceControlModule, LoreSourceControl);
