// =============================================================================
// Надстройка «Ведомость отделки» для Archicad 27
//   Меню «Параметры → Ведомость отделки…» — расчёт ведомости отделки помещений
//   по зонам HimeraZone (Otdelka.cpp; логика — FinishCore, Excel — XlsxWriter).
// =============================================================================

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "ResourceIds.hpp"
#include "Otdelka.hpp"

static const GSResID	AddOnInfoID		= ID_ADDON_INFO;
static const Int32		AddOnNameID		= 1;
static const Int32		AddOnDescID		= 2;
static const short		AddOnMenuID		= ID_ADDON_MENU;


static GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID == AddOnMenuID) {
		if (menuParams->menuItemRef.itemIndex == 1) DoOtdelka (false);
		else if (menuParams->menuItemRef.itemIndex == 2) DoOtdelka (true);
		else if (menuParams->menuItemRef.itemIndex == 3) DoSelectZones ();
	}
	return NoError;
}

// ============================================================================= обязательные функции

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, AddOnInfoID, AddOnNameID, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, AddOnInfoID, AddOnDescID, ACAPI_GetOwnResModule ());
	return APIAddon_Normal;
}


GSErrCode RegisterInterface (void)
{
	return ACAPI_MenuItem_RegisterMenu (AddOnMenuID, 0, MenuCode_Tools, MenuFlag_Default);
}


GSErrCode Initialize (void)
{
	return ACAPI_MenuItem_InstallMenuHandler (AddOnMenuID, MenuCommandHandler);
}


GSErrCode FreeData (void)
{
	return NoError;
}
