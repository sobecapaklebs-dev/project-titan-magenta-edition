// WindowToolbar.swift — Optional native NSToolbar for the MGB64 game window.
//
// Provides a macOS-native toolbar with quick-access buttons for performance
// overlay, screenshot, fullscreen, and volume control. The toolbar is hidden by
// default and can be shown via the View menu (Cmd+Opt+T).
//
// Integration:
//   Attach to the window via window.toolbar = WindowToolbarManager.createToolbar()
//
// The toolbar uses the responder chain (nil target) so that action methods are
// dispatched to whichever object in the chain implements them — typically
// AppDelegate. See the Selector extensions at the bottom for the expected
// action selectors.

import Cocoa

// MARK: - Toolbar Item Identifiers

extension NSToolbarItem.Identifier {
    /// Toggles the performance overlay HUD (FPS / frame time).
    static let performanceToggle = NSToolbarItem.Identifier("performanceToggle")

    /// Captures a screenshot of the current frame.
    static let screenshotButton = NSToolbarItem.Identifier("screenshotButton")

    /// Toggles native macOS fullscreen on the game window.
    static let fullscreenToggle = NSToolbarItem.Identifier("fullscreenToggle")

    /// An inline volume slider (0–100) for adjusting audio output.
    static let volumeSlider = NSToolbarItem.Identifier("volumeSlider")
}

// MARK: - Action Selectors

extension Selector {
    /// Toggles the performance overlay visibility.
    static let togglePerformanceOverlay = #selector(AppDelegate.togglePerformanceOverlay(_:))

    /// Captures a screenshot of the current game frame.
    static let captureScreenshot = #selector(AppDelegate.captureScreenshot(_:))

    /// Toggles fullscreen (standard AppKit selector).
    static let toggleFullScreen = #selector(NSWindow.toggleFullScreen(_:))
}

// MARK: - WindowToolbarManager

/// Creates and manages an NSToolbar for the main game window.
///
/// The toolbar is configured for macOS 14+ with icon-only display, user
/// customisation support, and automatic configuration persistence. It is
/// hidden by default; use the View > Show Toolbar menu item (Cmd+Opt+T) to
/// reveal it.
///
/// Usage:
/// ```swift
/// window.toolbar = WindowToolbarManager.createToolbar()
/// ```
final class WindowToolbarManager: NSObject, NSToolbarDelegate {

    // MARK: - Shared Instance

    /// A long-lived delegate instance. NSToolbar holds its delegate weakly, so
    /// this must be kept alive for the lifetime of the toolbar.
    private static let shared = WindowToolbarManager()

    // MARK: - Constants

    /// All toolbar item identifiers that the user may add via customisation.
    private static let allItemIdentifiers: [NSToolbarItem.Identifier] = [
        .performanceToggle,
        .screenshotButton,
        .fullscreenToggle,
        .volumeSlider,
        .flexibleSpace,
        .space
    ]

    /// The default set of toolbar items shown before the user customises.
    private static let defaultItemIdentifiers: [NSToolbarItem.Identifier] = [
        .flexibleSpace,
        .performanceToggle,
        .screenshotButton,
        .fullscreenToggle
    ]

    // MARK: - Public API

    /// Creates a fully configured NSToolbar ready to attach to a window.
    ///
    /// The toolbar is hidden by default (`isVisible = false`). To show it,
    /// call `window.toggleToolbarShown(_:)` or use the View menu item.
    ///
    /// - Returns: A configured `NSToolbar` instance.
    static func createToolbar() -> NSToolbar {
        let toolbar = NSToolbar(identifier: "com.mgb64.mainToolbar")
        toolbar.delegate = shared
        toolbar.displayMode = .iconOnly
        toolbar.allowsUserCustomization = true
        toolbar.autosavesConfiguration = true

        if #available(macOS 14.0, *) {
            toolbar.sizeMode = .regular
        }

        // Hidden by default — the user can reveal it from the View menu.
        toolbar.isVisible = false

        return toolbar
    }

    /// Adds a "Show Toolbar" menu item (Cmd+Opt+T) to the View menu.
    ///
    /// Call this after `MenuBarManager.setupMainMenu(appName:)` so that the
    /// View menu already exists. The item uses the standard AppKit
    /// `toggleToolbarShown:` action which automatically updates its title
    /// between "Show Toolbar" and "Hide Toolbar".
    static func installViewMenuItem() {
        guard let mainMenu = NSApp.mainMenu else { return }

        // Find the View menu by title.
        guard let viewMenuItem = mainMenu.item(withTitle: "View"),
              let viewMenu = viewMenuItem.submenu else { return }

        viewMenu.addItem(.separator())

        let toolbarItem = NSMenuItem(
            title: "Show Toolbar",
            action: #selector(NSWindow.toggleToolbarShown(_:)),
            keyEquivalent: "t"
        )
        toolbarItem.keyEquivalentModifierMask = [.command, .option]
        viewMenu.addItem(toolbarItem)
    }

    // MARK: - NSToolbarDelegate

    func toolbarAllowedItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        Self.allItemIdentifiers
    }

    func toolbarDefaultItemIdentifiers(_ toolbar: NSToolbar) -> [NSToolbarItem.Identifier] {
        Self.defaultItemIdentifiers
    }

    func toolbar(
        _ toolbar: NSToolbar,
        itemForItemIdentifier itemIdentifier: NSToolbarItem.Identifier,
        willBeInsertedIntoToolbar flag: Bool
    ) -> NSToolbarItem? {

        switch itemIdentifier {
        case .performanceToggle:
            return makePerformanceToggleItem(identifier: itemIdentifier)

        case .screenshotButton:
            return makeScreenshotItem(identifier: itemIdentifier)

        case .fullscreenToggle:
            return makeFullscreenToggleItem(identifier: itemIdentifier)

        case .volumeSlider:
            return makeVolumeSliderItem(identifier: itemIdentifier)

        default:
            return nil
        }
    }

    // MARK: - Item Factory Methods

    /// Creates the performance overlay toggle toolbar item.
    private func makePerformanceToggleItem(
        identifier: NSToolbarItem.Identifier
    ) -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: identifier)
        item.label = "Performance"
        item.paletteLabel = "Performance Overlay"
        item.toolTip = "Toggle the performance overlay (FPS and frame timing)"
        item.image = NSImage(systemSymbolName: "speedometer", accessibilityDescription: "Performance")
        item.target = nil  // Responder chain
        item.action = .togglePerformanceOverlay
        item.isBordered = true
        return item
    }

    /// Creates the screenshot capture toolbar item.
    private func makeScreenshotItem(
        identifier: NSToolbarItem.Identifier
    ) -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: identifier)
        item.label = "Screenshot"
        item.paletteLabel = "Capture Screenshot"
        item.toolTip = "Capture a screenshot of the current frame"
        item.image = NSImage(systemSymbolName: "camera", accessibilityDescription: "Screenshot")
        item.target = nil  // Responder chain
        item.action = .captureScreenshot
        item.isBordered = true
        return item
    }

    /// Creates the fullscreen toggle toolbar item.
    private func makeFullscreenToggleItem(
        identifier: NSToolbarItem.Identifier
    ) -> NSToolbarItem {
        let item = NSToolbarItem(itemIdentifier: identifier)
        item.label = "Fullscreen"
        item.paletteLabel = "Toggle Fullscreen"
        item.toolTip = "Toggle fullscreen mode"
        item.image = NSImage(
            systemSymbolName: "arrow.up.left.and.arrow.down.right",
            accessibilityDescription: "Fullscreen"
        )
        item.target = nil  // Responder chain
        item.action = .toggleFullScreen
        item.isBordered = true
        return item
    }

    /// Creates the volume slider toolbar item.
    ///
    /// The slider ranges from 0 to 100 and dispatches value changes through
    /// the responder chain to a `volumeDidChange:` action on AppDelegate.
    private func makeVolumeSliderItem(
        identifier: NSToolbarItem.Identifier
    ) -> NSToolbarItem {
        let slider = NSSlider(value: 100, minValue: 0, maxValue: 100, target: nil, action: #selector(AppDelegate.volumeDidChange(_:)))
        slider.controlSize = .small
        slider.frame = NSRect(x: 0, y: 0, width: 120, height: 24)
        slider.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            slider.widthAnchor.constraint(equalToConstant: 140),
            slider.heightAnchor.constraint(equalToConstant: 24)
        ])

        let item = NSToolbarItem(itemIdentifier: identifier)
        item.label = "Volume"
        item.paletteLabel = "Volume Slider"
        item.toolTip = "Adjust audio volume (0–100)"
        item.view = slider
        return item
    }
}

// MARK: - AppDelegate Toolbar Action Extensions

/// Toolbar action selectors dispatched via the responder chain. These extend
/// AppDelegate so the responder chain naturally finds them when the toolbar
/// items have a nil target.
extension AppDelegate {

    /// Toggles the performance overlay visibility.
    @objc func togglePerformanceOverlay(_ sender: Any?) {
        guard let contentView = window?.contentView else { return }

        if performanceOverlay == nil {
            let overlay = PerformanceOverlayView(
                frame: NSRect(x: contentView.bounds.width - 190,
                              y: contentView.bounds.height - 90,
                              width: 180, height: 80)
            )
            contentView.addSubview(overlay)
            performanceOverlay = overlay
        }

        if let overlay = performanceOverlay {
            if overlay.isVisible {
                overlay.hide()
            } else {
                overlay.show()
            }
        }

        // Persist the preference
        UserDefaults.standard.set(
            performanceOverlay?.isVisible ?? false,
            forKey: "advanced.performanceOverlay"
        )
    }

    // captureScreenshot(_:) is defined in ScreenshotManager.swift

    /// Called when the volume slider value changes.
    ///
    /// - Parameter sender: The `NSSlider` whose value changed (0–100).
    @objc func volumeDidChange(_ sender: Any?) {
        guard let slider = sender as? NSSlider else { return }
        let volume = Float(slider.doubleValue) / 100.0
        // TODO: Pass normalised volume (0.0–1.0) to the audio engine via game bridge.
        _ = volume
    }
}
