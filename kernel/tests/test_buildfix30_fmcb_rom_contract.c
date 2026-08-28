#include <assert.h>
#include <string.h>
#include "titan_installer_model.h"
static TitanInstallerConsoleInfo c(const char *rom, TitanInstallerConsoleFamily f){ TitanInstallerConsoleInfo i; memset(&i,0,sizeof(i)); i.model_id_valid=1; i.model_id=0xd426; i.family=f; strncpy(i.romver,rom,sizeof(i.romver)-1); return i; }
int main(void){
 TitanInstallerConsoleInfo a=c("0190E",TITAN_INSTALLER_CONSOLE_FAT); assert(TitanInstallerModel_IsAutonomousCompatible(&a));
 TitanInstallerConsoleInfo b=c("0200E",TITAN_INSTALLER_CONSOLE_SLIM_70); assert(TitanInstallerModel_IsAutonomousCompatible(&b));
 TitanInstallerConsoleInfo d=c("0220E",TITAN_INSTALLER_CONSOLE_SLIM_70); assert(TitanInstallerModel_IsAutonomousCompatible(&d));
 TitanInstallerConsoleInfo e=c("0221E",TITAN_INSTALLER_CONSOLE_SLIM_70); assert(!TitanInstallerModel_IsAutonomousCompatible(&e));
 TitanInstallerConsoleInfo f=c("0230E",TITAN_INSTALLER_CONSOLE_SLIM_90); assert(!TitanInstallerModel_IsAutonomousCompatible(&f));
 TitanInstallerConsoleInfo u=c("",TITAN_INSTALLER_CONSOLE_SLIM_70); assert(!TitanInstallerModel_IsAutonomousCompatible(&u));
 TitanInstallerConsoleInfo r=c("0220Z",TITAN_INSTALLER_CONSOLE_SLIM_70); assert(!TitanInstallerModel_IsAutonomousCompatible(&r));
 return 0;
}
