# Art Department HOME-entry Patch Integration — BuildFix30

The Art Department hardening patch was audited as behavioral evidence, not adopted as a permanent dependency. It demonstrated four useful requirements: system/Ribbon gating before HOME, scene opacity tied to real HOME entry, a HOME-only music policy, and preservation of Theme v1-v3 behavior.

BuildFix30 absorbs those requirements into the official architecture with an explicit Theme Lifecycle service. The resulting implementation is global for compatible dynamic themes and contains no GTA VI theme ID, package name, asset path, delay timer, or theme-specific branch.

BuildFix30 additionally hardens resource ownership beyond the reference patch: `RefreshThemeAssets()` only invalidates dynamic readiness; dynamic filesystem reads run on Titan's serialized storage worker after lifecycle authorization. Rendering sees no partially initialized scene and falls back to the Ribbon while activation is incomplete.

The external patch is therefore provenance/reference material only and is not required to build, boot, run, or rehydrate BuildFix30.
