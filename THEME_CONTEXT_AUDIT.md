# Theme Context Audit — BuildFix23

## CP08 canonical contexts
`THEME_CONTEXT_HOME`: full scene/artwork/character/decorations/animations.
`THEME_CONTEXT_SYSTEM`: active-theme base palette + theme waves/Ribbon + functional UI; no full HOME artwork.

The existing Settings behavior remains canonical. Startup health/Welcome/Profile Selection already renders SYSTEM Flow/Ribbon, while HOME_ENTER is the boundary where full HOME experience is revealed. Functional TitanShellPage routes use `theme_system_view_transition`; TOP MENU remains HOME context.

A formal `TitanThemeContext` + `TitanThemeContextPolicy` now records this contract without duplicating the renderer abstraction or changing legacy theme packages.

## CP09 transitions / palette / accent
- HOME↔SYSTEM uses complementary scene/ribbon alpha driven by one normalized transition amount.
- Active theme shell accent remains the source for focus chrome; no universal magenta accent is introduced.
- Theme generation refresh remains the asset/style invalidation boundary.
- SYSTEM→SYSTEM does not reveal HOME artwork because system amount remains at the SYSTEM endpoint; startup→HOME reveals artwork only during HOME_ENTER.
