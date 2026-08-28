#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../src/project_nes_core.h"
#include "../src/project_nes.c"

static int provider_ctx_marker;
static int fake_load(void *c, uint8_t *d, uint32_t s){ (void)c;(void)d;(void)s; return 1; }
static void fake_reset(void *c){ (void)c; }
static void fake_input(void *c,const ProjectNesCoreInput *i){ (void)c;(void)i; }
static int fake_frame(void *c,const ProjectNesCoreVideoTarget *v){ (void)c;(void)v; return 1; }
static uint32_t fake_rate(void *c){ (void)c; return 60000u; }
static uint32_t fake_next(void *c){ (void)c; return 533u; }
static uint32_t fake_audio(void *c,int16_t *s,uint32_t n){ (void)c;(void)s; return n; }
static int fake_probe(void *c,const uint8_t *d,uint32_t n,ProjectNesCoreRomInfo *i){ (void)c;(void)d;(void)n;(void)i; return 1; }

static ProjectNesCoreBackend good_backend = {
    "FCEUmm test provider", fake_load, fake_reset, fake_input, fake_frame,
    fake_rate, fake_next, fake_audio, 0,0,0,0,0, fake_probe, 0
};
static ProjectNesProviderDescriptor descriptor = {
    PROJECT_NES_PROVIDER_ABI_VERSION, sizeof(ProjectNesProviderDescriptor),
    "FCEUmm", "GPL-2.0-only",
    "b5e3566515c27dc66c9c20572171673126532e06",
    &good_backend, &provider_ctx_marker
};

static void expect_fallback(void)
{
    assert(g_pn.backend == &g_pn_legacy_backend);
    assert(pn_backend_ctx() == &g_pn.nes);
    assert(g_pn.provider == 0);
}
int main(void)
{
    const char *saved_license=descriptor.license_spdx;
    const char *saved_commit=descriptor.source_commit;
    int (*saved_frame)(void *,const ProjectNesCoreVideoTarget *)=good_backend.run_frame;

    memset(&g_pn,0,sizeof(g_pn)); pn_select_backend_candidate(&descriptor);
    assert(g_pn.backend==&good_backend && g_pn.backend_context==&provider_ctx_marker && g_pn.provider==&descriptor);

    descriptor.license_spdx="MIT"; memset(&g_pn,0,sizeof(g_pn)); pn_select_backend_candidate(&descriptor); expect_fallback(); descriptor.license_spdx=saved_license;
    descriptor.source_commit="0000000000000000000000000000000000000000"; memset(&g_pn,0,sizeof(g_pn)); pn_select_backend_candidate(&descriptor); expect_fallback(); descriptor.source_commit=saved_commit;
    good_backend.run_frame=0; memset(&g_pn,0,sizeof(g_pn)); pn_select_backend_candidate(&descriptor); expect_fallback(); good_backend.run_frame=saved_frame;
    descriptor.abi_version=99u; memset(&g_pn,0,sizeof(g_pn)); pn_select_backend_candidate(&descriptor); expect_fallback(); descriptor.abi_version=PROJECT_NES_PROVIDER_ABI_VERSION;
    memset(&g_pn,0,sizeof(g_pn)); pn_select_backend_candidate(0); expect_fallback();
    puts("project-nes-provider-selection: OK valid frozen GPL provider selected; invalid/missing provider falls back");
    return 0;
}
