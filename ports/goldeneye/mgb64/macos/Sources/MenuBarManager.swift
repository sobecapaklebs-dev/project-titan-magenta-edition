// MenuBarManager.swift — Programmatic NSMenu setup for the macOS app.
//
// Creates the full menu bar hierarchy conforming to the macOS Human Interface
// Guidelines. Called once during applicationDidFinishLaunching to replace the
// default empty menu bar with application-specific menus.

import Cocoa

// MARK: - MenuBarManager

/// Builds and installs the application's main menu bar programmatically.
///
/// Since this app does not use a storyboard or XIB, the entire menu bar must be
/// constructed in code. `setupMainMenu(appName:)` creates the standard menu
/// structure (App, Game, View, Window, Help) and assigns it to `NSApp.mainMenu`.
final class MenuBarManager {

    // MARK: - Public API

    /// Creates the complete menu bar and installs it as the application's main menu.
    ///
    /// - Parameter appName: The display name of the application, used in menu
    ///   titles such as "About [appName]", "Hide [appName]", and "Quit [appName]".
    static func setupMainMenu(appName: String) {
        let mainMenu = NSMenu(title: "Main Menu")

        mainMenu.addItem(buildAppMenuItem(appName: appName))
        mainMenu.addItem(buildGameMenuItem())
        mainMenu.addItem(buildViewMenuItem())
        mainMenu.addItem(buildWindowMenuItem())
        mainMenu.addItem(buildHelpMenuItem(appName: appName))

        NSApp.mainMenu = mainMenu
    }

    // MARK: - App Menu

    /// Builds the application menu (the bold-titled menu to the left of "File").
    private static func buildAppMenuItem(appName: String) -> NSMenuItem {
        let appMenuItem = NSMenuItem()
        let appMenu = NSMenu(title: appName)

        // About — uses custom branded About panel
        let aboutItem = NSMenuItem(
            title: "About \(appName)",
            action: #selector(AppDelegate.showAboutPanel(_:)),
            keyEquivalent: ""
        )
        appMenu.addItem(aboutItem)

        appMenu.addItem(.separator())

        // Preferences...
        let preferencesItem = NSMenuItem(
            title: "Preferences\u{2026}",
            action: #selector(AppDelegate.showPreferences(_:)),
            keyEquivalent: ","
        )
        appMenu.addItem(preferencesItem)

        appMenu.addItem(.separator())

        // Hide [appName]
        let hideItem = NSMenuItem(
            title: "Hide \(appName)",
            action: #selector(NSApplication.hide(_:)),
            keyEquivalent: "h"
        )
        appMenu.addItem(hideItem)

        // Hide Others
        let hideOthersItem = NSMenuItem(
            title: "Hide Others",
            action: #selector(NSApplication.hideOtherApplications(_:)),
            keyEquivalent: "h"
        )
        hideOthersItem.keyEquivalentModifierMask = [.option, .command]
        appMenu.addItem(hideOthersItem)

        // Show All
        let showAllItem = NSMenuItem(
            title: "Show All",
            action: #selector(NSApplication.unhideAllApplications(_:)),
            keyEquivalent: ""
        )
        appMenu.addItem(showAllItem)

        appMenu.addItem(.separator())

        // Quit [appName]
        let quitItem = NSMenuItem(
            title: "Quit \(appName)",
            action: #selector(NSApplication.terminate(_:)),
            keyEquivalent: "q"
        )
        appMenu.addItem(quitItem)

        appMenuItem.submenu = appMenu
        return appMenuItem
    }

    // MARK: - Game Menu

    /// Builds the Game menu with pause/resume and ROM selection.
    private static func buildGameMenuItem() -> NSMenuItem {
        let gameMenuItem = NSMenuItem()
        let gameMenu = NSMenu(title: "Game")

        // Pause / Resume
        let pauseItem = NSMenuItem(
            title: "Pause",
            action: #selector(AppDelegate.togglePause(_:)),
            keyEquivalent: "p"
        )
        gameMenu.addItem(pauseItem)

        gameMenu.addItem(.separator())

        // Capture Screenshot
        let screenshotItem = NSMenuItem(
            title: "Capture Screenshot",
            action: #selector(AppDelegate.captureScreenshot(_:)),
            keyEquivalent: "s"
        )
        screenshotItem.keyEquivalentModifierMask = [.shift, .command]
        gameMenu.addItem(screenshotItem)

        gameMenu.addItem(.separator())

        // Select ROM...
        let selectROMItem = NSMenuItem(
            title: "Select ROM\u{2026}",
            action: #selector(AppDelegate.selectROM(_:)),
            keyEquivalent: ""
        )
        gameMenu.addItem(selectROMItem)

        gameMenuItem.submenu = gameMenu
        return gameMenuItem
    }

    // MARK: - View Menu

    /// Builds the View menu with fullscreen toggle and size presets.
    private static func buildViewMenuItem() -> NSMenuItem {
        let viewMenuItem = NSMenuItem()
        let viewMenu = NSMenu(title: "View")

        // Enter Full Screen
        let fullScreenItem = NSMenuItem(
            title: "Enter Full Screen",
            action: #selector(NSWindow.toggleFullScreen(_:)),
            keyEquivalent: "f"
        )
        fullScreenItem.keyEquivalentModifierMask = [.control, .command]
        viewMenu.addItem(fullScreenItem)

        viewMenu.addItem(.separator())

        // Actual Size (1x = 320x240)
        let actualSizeItem = NSMenuItem(
            title: "Actual Size",
            action: #selector(AppDelegate.setWindowSize1x(_:)),
            keyEquivalent: "1"
        )
        viewMenu.addItem(actualSizeItem)

        // Double Size (2x = 640x480)
        let doubleSizeItem = NSMenuItem(
            title: "Double Size",
            action: #selector(AppDelegate.setWindowSize2x(_:)),
            keyEquivalent: "2"
        )
        viewMenu.addItem(doubleSizeItem)

        // Triple Size (3x = 960x720)
        let tripleSizeItem = NSMenuItem(
            title: "Triple Size",
            action: #selector(AppDelegate.setWindowSize3x(_:)),
            keyEquivalent: "3"
        )
        viewMenu.addItem(tripleSizeItem)

        viewMenuItem.submenu = viewMenu
        return viewMenuItem
    }

    // MARK: - Window Menu

    /// Builds the Window menu with standard Minimize and Zoom items.
    ///
    /// Assigning the menu to `NSApp.windowsMenu` tells AppKit to automatically
    /// append the list of open windows at the bottom.
    private static func buildWindowMenuItem() -> NSMenuItem {
        let windowMenuItem = NSMenuItem()
        let windowMenu = NSMenu(title: "Window")

        // Minimize
        let minimizeItem = NSMenuItem(
            title: "Minimize",
            action: #selector(NSWindow.performMiniaturize(_:)),
            keyEquivalent: "m"
        )
        windowMenu.addItem(minimizeItem)

        // Zoom
        let zoomItem = NSMenuItem(
            title: "Zoom",
            action: #selector(NSWindow.performZoom(_:)),
            keyEquivalent: ""
        )
        windowMenu.addItem(zoomItem)

        windowMenuItem.submenu = windowMenu

        // Let AppKit manage the window list in this menu.
        NSApp.windowsMenu = windowMenu

        return windowMenuItem
    }

    // MARK: - Help Menu

    /// Builds the Help menu with application help and issue reporting.
    private static func buildHelpMenuItem(appName: String) -> NSMenuItem {
        let helpMenuItem = NSMenuItem()
        let helpMenu = NSMenu(title: "Help")

        // [appName] Help
        let helpItem = NSMenuItem(
            title: "\(appName) Help",
            action: #selector(NSApplication.showHelp(_:)),
            keyEquivalent: ""
        )
        helpMenu.addItem(helpItem)

        helpMenu.addItem(.separator())

        // Report Issue...
        let reportItem = NSMenuItem(
            title: "Report Issue\u{2026}",
            action: #selector(AppDelegate.reportIssue(_:)),
            keyEquivalent: ""
        )
        helpMenu.addItem(reportItem)

        helpMenuItem.submenu = helpMenu

        // Let AppKit manage Help menu search.
        NSApp.helpMenu = helpMenu

        return helpMenuItem
    }
}

// MARK: - AppDelegate Menu Action Extensions

/// Menu action selectors referenced by the menu bar. These extend AppDelegate so
/// the responder chain naturally finds them. Actions that are not yet implemented
/// in AppDelegate are stubbed here.
extension AppDelegate {

    /// Toggles game pause/resume. The menu item title should update to reflect
    /// the current state.
    @objc func togglePause(_ sender: Any?) {
        // TODO: Implement pause/resume toggle via game bridge.
    }

    /// Sets the window content size to 1x native resolution (320x240).
    @objc func setWindowSize1x(_ sender: Any?) {
        setWindowContentSize(width: 320, height: 240)
    }

    /// Sets the window content size to 2x native resolution (640x480).
    @objc func setWindowSize2x(_ sender: Any?) {
        setWindowContentSize(width: 640, height: 480)
    }

    /// Sets the window content size to 3x native resolution (960x720).
    @objc func setWindowSize3x(_ sender: Any?) {
        setWindowContentSize(width: 960, height: 720)
    }

    /// Opens the project's GitHub issue tracker in the default browser.
    @objc func reportIssue(_ sender: Any?) {
        NSWorkspace.shared.open(Brand.issuesURL)
    }

    /// Helper to resize the main window's content area to exact pixel dimensions.
    private func setWindowContentSize(width: CGFloat, height: CGFloat) {
        guard let window = NSApp.mainWindow ?? NSApp.windows.first else { return }
        let contentSize = NSSize(width: width, height: height)
        window.setContentSize(contentSize)
        window.center()
    }
}
