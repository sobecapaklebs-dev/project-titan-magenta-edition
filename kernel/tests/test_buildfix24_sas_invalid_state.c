#include "titan_applications.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    TitanApplication a = {0};
    assert(TitanApplications_IsSasStateValid(&a));
    a.sas_compatible=1u; a.sas_compliant=0u;
    assert(TitanApplications_IsSasStateValid(&a));
    a.sas_compatible=1u; a.sas_compliant=1u;
    assert(TitanApplications_IsSasStateValid(&a));
    a.sas_compatible=0u; a.sas_compliant=1u;
    assert(!TitanApplications_IsSasStateValid(&a));
    puts("titan-sas-invalid-compliance-state-contract: OK");
    return 0;
}
