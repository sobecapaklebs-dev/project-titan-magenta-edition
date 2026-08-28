/* TitanEYE deliberately disables the optional desktop glTF decoration layer.
 * It is a remaster-only file-system feature, not part of the retail game, and
 * would otherwise pull cgltf, stb_image and unrestricted host paths into TGR.
 * decor_native.c is retained unchanged; with g_pcSceneDecor == 0 it emits no
 * commands, while these closed stubs make accidental activation fail safely. */
#include "mgb64/src/platform/decor_assets.h"

#include <string.h>

int decorAssetsLoadLevel(const char *dir, const char *slug, DecorLevel *out)
{
    (void)dir;
    (void)slug;
    if (out != 0) memset(out, 0, sizeof(*out));
    return 0;
}

void decorAssetsFree(DecorLevel *level)
{
    if (level != 0) memset(level, 0, sizeof(*level));
}
