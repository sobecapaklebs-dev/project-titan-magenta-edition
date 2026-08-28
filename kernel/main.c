#include "titan_kernel.h"
#include "titan_installer.h"

int main(
    int argc,
    char *argv[]
)
{
    if (TitanInstaller_ShouldRun(argc, argv))
    {
        return TitanInstaller_Run(argc, argv);
    }

    return TitanKernel_Run(
        argc,
        argv
    );
}
