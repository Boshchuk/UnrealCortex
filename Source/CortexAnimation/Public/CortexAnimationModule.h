#pragma once

#include "Modules/ModuleInterface.h"

DECLARE_LOG_CATEGORY_EXTERN(LogCortexAnimation, Log, All);

class FCortexAnimationModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
