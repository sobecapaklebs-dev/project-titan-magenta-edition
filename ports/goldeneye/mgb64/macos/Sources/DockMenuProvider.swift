// DockMenuProvider.swift — Right-click dock icon menu for MGB64.
//
// Provides a contextual menu when the user right-clicks (or control-clicks) the
// app's Dock icon. Menu actions are dispatched through the responder chain using
// the same selectors as the main menu bar, so no additional action methods are
// needed here.
//
// Integration: AppDelegate implements applicationDockMenu(_:) and delegates to
// DockMenuProvider.createDockMenu(). See the AppDelegate extension at the bottom
// of this file.

import Cocoa

// MARK: - DockMenuProvider

/// Builds the contextual menu shown when the user right-clicks the Dock icon.
///
/// Menu items target `nil` so that AppKit walks the responder chain to find the
/// appropriate action handler (typically AppDelegate). This keeps the provider
/// stateless and free of coupling to any specific target object.
final class DockMenuProvider {

    // MARK: - Public API

    /// Creates and returns a fresh dock menu.
    ///
    /// A new `NSMenu` is built on each call because AppKit may request the dock
    /// menu at any time, and the enabled state of items (e.g., "Resume Game")
    /// can change between invocations.
    ///
    /// - Returns: An `NSMenu` configured with dock-appropriate items.
    func createDockMenu() -> NSMenu {
        let menu = NSMenu()

        // Resume Game — only meaningful when the game loop is active.
        let resumeItem = NSMenuItem(
            title: "Resume Game",
            action: #selector(AppDelegate.togglePause(_:)),
            keyEquivalent: ""
        )
        resumeItem.target = nil  // Responder chain
        resumeItem.isEnabled = game_is_running()
        menu.addItem(resumeItem)

        menu.addItem(.separator())

        // Preferences...
        let preferencesItem = NSMenuItem(
            title: "Preferences\u{2026}",
            action: #selector(AppDelegate.showPreferences(_:)),
            keyEquivalent: ""
        )
        preferencesItem.target = nil
        menu.addItem(preferencesItem)

        // Select ROM...
        let selectROMItem = NSMenuItem(
            title: "Select ROM\u{2026}",
            action: #selector(AppDelegate.selectROM(_:)),
            keyEquivalent: ""
        )
        selectROMItem.target = nil
        menu.addItem(selectROMItem)

        return menu
    }
}

// MARK: - AppDelegate Dock Menu Integration

extension AppDelegate {

    /// Called by AppKit when the user right-clicks the Dock icon.
    ///
    /// Returns a freshly built menu from `DockMenuProvider` so that the enabled
    /// state of each item reflects the current application state.
    func applicationDockMenu(_ sender: NSApplication) -> NSMenu? {
        let provider = DockMenuProvider()
        return provider.createDockMenu()
    }
}
