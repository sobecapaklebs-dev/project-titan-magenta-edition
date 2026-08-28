// AppDelegate.swift — macOS application delegate for the MGB64 project.
//
// Manages the application lifecycle, main window, ROM selection, and game thread.
// The game engine runs on a dedicated background thread; all AppKit operations
// remain on the main thread. Communication with the engine uses the C functions
// declared in GameBridge.h (exposed via the bridging header).

import Cocoa
import GameController
import SwiftUI
import UniformTypeIdentifiers
import UserNotifications

// MARK: - Application Delegate

/// Primary application delegate that owns the main window, manages the game
/// lifecycle, and bridges between AppKit and the C game engine.
@main
final class AppDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {

    // MARK: Properties

    /// Convenience accessor used by external callers expecting `gameWindow`.
    var gameWindow: NSWindow? { window }

    /// The main game window. Created once in applicationDidFinishLaunching.
    var window: NSWindow?

    /// Token for preventing App Nap while the game engine is running.
    private var activityToken: NSObjectProtocol?

    /// The view that provides the CALayer / CAMetalLayer render surface.
    private var renderView: GameRenderView?

    /// The gamepad input manager for controller support.
    private var inputManager = InputManager()

    /// The dedicated thread on which game_init / game_run execute.
    private var gameThread: Thread?

    /// Performance overlay (FPS counter + frame time graph). Lazily created.
    var performanceOverlay: PerformanceOverlayView?

    /// The absolute path to the validated ROM file.
    private var romPath: String?

    /// UserDefaults key for the persisted ROM path.
    private static let romPathDefaultsKey = "romPath"

    // MARK: - Application Lifecycle

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Set up notification delegate for screenshot notifications
        UNUserNotificationCenter.current().delegate = self

        MenuBarManager.setupMainMenu(appName: Brand.shortName)
        inputManager.setup()

        // Show onboarding on first launch (before loading ROM)
        // After onboarding completes, the ROM picker is embedded in the flow

        createMainWindow()

        // Attach toolbar (hidden by default; toggled via View menu)
        window?.toolbar = WindowToolbarManager.createToolbar()
        window?.toolbar?.isVisible = false
        WindowToolbarManager.installViewMenuItem()

        if #available(macOS 14.0, *), !OnboardingFlow.isComplete {
            // Present onboarding as a sheet
            let onboardingView = OnboardingFlow(onFinished: { [weak self] romPath in
                guard let self = self else { return }
                if let sheet = self.window?.sheets.first {
                    self.window?.endSheet(sheet)
                }
                if let path = romPath,
                   FileManager.default.fileExists(atPath: path) {
                    UserDefaults.standard.set(path, forKey: Self.romPathDefaultsKey)
                    self.romPath = path
                    self.startGame()
                } else {
                    self.loadSavedROMPathAndStart()
                }
            })
            let hostingController = NSHostingController(rootView: onboardingView)
            let sheetWindow = NSWindow(contentViewController: hostingController)
            sheetWindow.setContentSize(NSSize(width: 700, height: 500))
            if let mainWindow = window {
                mainWindow.beginSheet(sheetWindow)
            }
        } else {
            loadSavedROMPathAndStart()
        }
    }

    func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool { true }

    func application(_ application: NSApplication, open urls: [URL]) {
        guard let romURL = urls.first(where: { ["z64", "v64", "n64"].contains($0.pathExtension.lowercased()) }) else { return }
        UserDefaults.standard.set(romURL.path, forKey: Self.romPathDefaultsKey)
        // If game isn't running yet, start it
        if !game_is_running() {
            romPath = romURL.path
            startGame()
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        return true
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        // End App Nap prevention
        if let token = activityToken {
            ProcessInfo.processInfo.endActivity(token)
            activityToken = nil
        }

        guard game_is_running() else {
            game_request_shutdown()
            return .terminateNow
        }

        let alert = NSAlert()
        alert.messageText = "Quit \(Brand.shortName)?"
        alert.informativeText = "Any unsaved progress will be lost."
        alert.alertStyle = .warning
        alert.addButton(withTitle: "Quit")
        alert.addButton(withTitle: "Cancel")

        if let window = window {
            alert.beginSheetModal(for: window) { response in
                if response == .alertFirstButtonReturn {
                    game_request_shutdown()
                    NSApp.reply(toApplicationShouldTerminate: true)
                } else {
                    NSApp.reply(toApplicationShouldTerminate: false)
                }
            }
            return .terminateLater
        } else {
            game_request_shutdown()
            return .terminateNow
        }
    }

    // MARK: - Window Creation

    /// Builds the main game window with sensible defaults for a 16:9 game viewport.
    private func createMainWindow() {
        let contentRect = NSRect(x: 0, y: 0, width: 960, height: 540)
        let styleMask: NSWindow.StyleMask = [
            .titled,
            .resizable,
            .closable,
            .miniaturizable,
            .fullSizeContentView
        ]

        let newWindow = NSWindow(
            contentRect: contentRect,
            styleMask: styleMask,
            backing: .buffered,
            defer: false
        )
        newWindow.title = Brand.shortName
        newWindow.setFrameAutosaveName("GameWindow")
        newWindow.minSize = NSSize(width: 480, height: 270)
        newWindow.delegate = self
        if !newWindow.setFrameUsingName("GameWindow") {
            newWindow.center()
        }
        newWindow.isReleasedWhenClosed = false

        let view = GameRenderView(frame: contentRect)
        newWindow.contentView = view
        renderView = view

        newWindow.makeKeyAndOrderFront(nil)
        window = newWindow
    }

    // MARK: - ROM Path Handling

    /// Checks UserDefaults for a previously saved ROM path. If the file still
    /// exists on disk, the game starts immediately; otherwise the ROM picker is
    /// presented.
    private func loadSavedROMPathAndStart() {
        if let savedPath = UserDefaults.standard.string(forKey: Self.romPathDefaultsKey),
           FileManager.default.fileExists(atPath: savedPath) {
            romPath = savedPath
            startGame()
        } else {
            presentROMPicker()
        }
    }

    /// Presents an open panel as a sheet attached to the main window, allowing
    /// the user to select a Nintendo 64 ROM file. The file is validated via the
    /// C bridge before being accepted.
    func presentROMPicker() {
        guard let window = window else { return }

        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        // Build UTTypes from file extensions for the ROM formats.
        var contentTypes: [UTType] = []
        for ext in ["z64", "v64", "n64"] {
            if let type = UTType(filenameExtension: ext) {
                contentTypes.append(type)
            }
        }
        panel.allowedContentTypes = contentTypes
        panel.message = "Select an N64 ROM file"
        panel.prompt = "Open ROM"

        panel.beginSheetModal(for: window) { [weak self] response in
            guard let self = self else { return }
            guard response == .OK, let url = panel.url else { return }

            let path = url.path
            let info = game_validate_rom(path)

            guard info.valid else {
                // info.message is imported as a fixed-size tuple of Int8.
                // Bridge it to a Swift String via its raw pointer.
                let message = withUnsafePointer(to: info.message) {
                    $0.withMemoryRebound(to: CChar.self, capacity: 128) {
                        String(cString: $0)
                    }
                }
                self.showAlert(
                    title: "Invalid ROM",
                    message: message
                )
                return
            }

            UserDefaults.standard.set(path, forKey: Self.romPathDefaultsKey)
            self.romPath = path
            self.startGame()
        }
    }

    // MARK: - Game Thread

    /// Configures the render surface, creates the save directory, and spawns the
    /// game thread. The game thread calls game_init followed by game_run, which
    /// blocks until shutdown is requested.
    private func startGame() {
        guard let view = renderView, let window = window else { return }
        guard let path = romPath else { return }

        // Provide the render surface before the game thread starts.
        let scale = Float(window.backingScaleFactor)
        let size = view.bounds.size
        let layer = view.layer

        game_set_render_surface(
            layer.map { Unmanaged.passUnretained($0).toOpaque() },
            Int32(size.width),
            Int32(size.height),
            scale
        )

        // Prevent App Nap while the game engine is running
        activityToken = ProcessInfo.processInfo.beginActivity(
            options: [.userInitiated, .latencyCritical],
            reason: "Game engine running"
        )

        let thread = Thread { [weak self] in
            self?.gameThreadMain(romPath: path)
        }
        thread.name = "GameThread"
        thread.qualityOfService = .userInteractive
        thread.start()
        gameThread = thread

        // Lock the mouse for FPS input after the game thread has had a moment
        // to initialize. Dispatched to the main thread to satisfy AppKit
        // threading requirements.
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { [weak self] in
            self?.renderView?.lockMouse()
        }
    }

    /// Entry point that runs on the game thread. Creates the save directory,
    /// initialises the engine, then enters the blocking game loop.
    private func gameThreadMain(romPath: String) {
        let saveDir = self.savesDirectoryPath()

        // Ensure the save directory exists.
        do {
            try FileManager.default.createDirectory(
                atPath: saveDir,
                withIntermediateDirectories: true,
                attributes: nil
            )
        } catch {
            NSLog("[GameThread] Failed to create save directory at %@: %@", saveDir, error.localizedDescription)
            // Continue anyway -- game_init may be able to cope or will report
            // its own error.
        }

        let initResult = game_init(romPath, saveDir)
        guard initResult == 0 else {
            NSLog("[GameThread] game_init failed with code %d", initResult)
            if #available(macOS 14.0, *) {
                DispatchQueue.main.async { [weak self] in
                    ErrorPresenter.showInitError(
                        message: "Failed to initialize the game engine. The ROM file may be incompatible.",
                        in: self?.window
                    ) { response in
                        if response == .quit {
                            NSApp.terminate(nil)
                        }
                    }
                }
            }
            return
        }

        game_run()
        NSLog("[GameThread] game_run returned; game thread exiting.")
    }

    /// Returns the path to ``~/Library/Application Support/MGB64/``.
    private func savesDirectoryPath() -> String {
        let appSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first

        // FileManager always provides at least one URL for applicationSupportDirectory.
        // Fall back to a temporary directory if something truly unexpected occurs.
        let base = appSupport?.path ?? NSTemporaryDirectory()
        return (base as NSString).appendingPathComponent(Brand.appSupportDirectoryName)
    }

    func applicationDidResignActive(_ notification: Notification) {
        // Unlock mouse when app loses focus so cursor is visible in other apps
        if let renderView = window?.contentView as? GameRenderView {
            renderView.unlockMouse()
        }
    }

    func applicationDidBecomeActive(_ notification: Notification) {
        // Re-lock mouse if game is running (user can click to re-lock via GameRenderView)
        // Don't auto-lock — let the user click to re-engage
    }

    func applicationWillTerminate(_ notification: Notification) {
        if let token = activityToken {
            ProcessInfo.processInfo.endActivity(token)
            activityToken = nil
        }
        // game_request_shutdown already called in applicationShouldTerminate
        // Give game thread brief time to finish current frame
        gameThread?.cancel()
        Thread.sleep(forTimeInterval: 0.1)
    }

    // MARK: - NSWindowDelegate

    func windowDidResize(_ notification: Notification) {
        guard let window = notification.object as? NSWindow,
              let view = window.contentView else { return }

        let size = view.bounds.size
        let scale = Float(window.backingScaleFactor)
        game_notify_resize(Int32(size.width), Int32(size.height), scale)
    }

    func windowDidChangeBackingProperties(_ notification: Notification) {
        // Retina / non-Retina transition (e.g., dragging between displays).
        guard let window = notification.object as? NSWindow,
              let view = window.contentView else { return }

        let size = view.bounds.size
        let scale = Float(window.backingScaleFactor)
        game_notify_resize(Int32(size.width), Int32(size.height), scale)
    }

    // MARK: - Menu Actions

    /// Toggles native macOS fullscreen on the main window.
    @objc func toggleFullscreen(_ sender: Any?) {
        window?.toggleFullScreen(sender)
    }

    /// Presents the Preferences window, or brings an existing one to front.
    @objc func showPreferences(_ sender: Any?) {
        guard #available(macOS 14.0, *) else {
            let alert = NSAlert()
            alert.alertStyle = .informational
            alert.messageText = "Settings Require macOS 14"
            alert.informativeText = "The native settings window uses macOS 14 controls. Edit ge007.ini directly on this system."
            alert.addButton(withTitle: "OK")
            if let window = window {
                alert.beginSheetModal(for: window)
            } else {
                alert.runModal()
            }
            return
        }

        if let existing = NSApp.windows.first(where: { $0.identifier?.rawValue == "Preferences" }) {
            existing.makeKeyAndOrderFront(nil)
            return
        }
        let prefsView = PreferencesView()
        let hostingController = NSHostingController(rootView: prefsView)
        let prefsWindow = NSWindow(contentViewController: hostingController)
        prefsWindow.identifier = NSUserInterfaceItemIdentifier("Preferences")
        prefsWindow.title = "\(Brand.shortName) Settings"
        prefsWindow.styleMask = [.titled, .closable]
        prefsWindow.setContentSize(NSSize(width: 520, height: 480))
        prefsWindow.center()
        prefsWindow.makeKeyAndOrderFront(nil)
    }

    /// Opens the custom About panel.
    @objc func showAboutPanel(_ sender: Any?) {
        if #available(macOS 14.0, *) {
            AboutView.show()
        }
    }

    /// Allows the user to pick a different ROM without restarting the app.
    /// If the game is already running, it is shut down first.
    @objc func selectROM(_ sender: Any?) {
        if game_is_running() {
            game_request_shutdown()
        }
        presentROMPicker()
    }

    /// Terminates the application through the standard AppKit path.
    @objc func quitApp(_ sender: Any?) {
        NSApplication.shared.terminate(sender)
    }

    // MARK: - Utilities

    /// Presents a modal alert on the main window with a single OK button.
    private func showAlert(title: String, message: String) {
        let alert = NSAlert()
        alert.messageText = title
        alert.informativeText = message
        alert.alertStyle = .warning
        alert.addButton(withTitle: "OK")

        if let window = window {
            alert.beginSheetModal(for: window, completionHandler: nil)
        } else {
            alert.runModal()
        }
    }
}
