// ScreenshotManager.swift — In-app screenshot capture with native macOS integration.
//
// Captures the game window contents, saves as PNG to ~/Pictures/{Brand.appName}/,
// plays the system screenshot sound, posts a notification, and triggers a brief
// white flash animation on the window. Integrates with the menu system via
// Cmd+Shift+S and provides a toolbar button hook.

import Cocoa
import UniformTypeIdentifiers
import UserNotifications

// MARK: - ScreenshotManager

/// Manages screenshot capture, saving, notification, and flash animation for the
/// game window. All public methods must be called on the main thread.
final class ScreenshotManager {

    // MARK: Properties

    /// Directory where screenshots are saved: ~/Pictures/{Brand.appName}/
    let savePath: URL

    /// Whether the white flash overlay is currently animating.
    private(set) var isFlashAnimating = false

    /// Date formatter for generating screenshot filenames.
    private let filenameFormatter: DateFormatter = {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd_HH-mm-ss"
        formatter.locale = Locale(identifier: "en_US_POSIX")
        return formatter
    }()

    // MARK: - Initialisation

    /// Creates the screenshot manager and ensures the save directory exists.
    init() {
        savePath = ScreenshotManager.screenshotDirectory()

        // Create the save directory if it does not already exist.
        let fm = FileManager.default
        if !fm.fileExists(atPath: savePath.path) {
            do {
                try fm.createDirectory(at: savePath, withIntermediateDirectories: true)
            } catch {
                NSLog("[ScreenshotManager] Failed to create screenshot directory at %@: %@",
                      savePath.path, error.localizedDescription)
            }
        }
    }

    // MARK: - Public API

    /// Captures a screenshot of the given window, saves it as PNG, plays the
    /// system screenshot sound, posts a user notification, and flashes the window.
    ///
    /// - Parameter window: The NSWindow to capture.
    func captureScreenshot(from window: NSWindow) {
        // 1. Validate that the window is backed by the window server.
        guard window.windowNumber > 0 else {
            NSLog("[ScreenshotManager] Invalid window number; cannot capture screenshot.")
            return
        }

        // 2. Capture the screen pixels occupied by the window. This avoids
        // CGWindowListCreateImage, which is unavailable for modern macOS
        // deployment targets.
        guard let cgImage = captureWindowImage(window) else {
            NSLog("[ScreenshotManager] Window image capture returned nil.")
            return
        }

        // 3. Generate the filename: Brand.appName_YYYY-MM-DD_HH-mm-ss.png
        let timestamp = filenameFormatter.string(from: Date())
        let filename = "\(Brand.appName)_\(timestamp).png"
        let fileURL = savePath.appendingPathComponent(filename)

        // 4. Save as PNG using CGImageDestination.
        guard savePNG(cgImage, to: fileURL) else {
            NSLog("[ScreenshotManager] Failed to save screenshot to %@.", fileURL.path)
            return
        }

        NSLog("[ScreenshotManager] Screenshot saved: %@", fileURL.path)

        // 5. Play the system screenshot sound.
        playScreenshotSound()

        // 6. Post a user notification with the saved image.
        postNotification(filename: filename, fileURL: fileURL)

        // 7. Trigger a brief white flash animation on the window.
        flashWindow(window)
    }

    /// Reveals the file at the given path in Finder.
    ///
    /// - Parameter path: Absolute filesystem path to reveal.
    func revealInFinder(path: String) {
        NSWorkspace.shared.selectFile(path, inFileViewerRootedAtPath: "")
    }

    /// Returns the URL of the screenshot directory: ~/Pictures/{Brand.appName}/
    static func screenshotDirectory() -> URL {
        let picturesURL = FileManager.default.urls(
            for: .picturesDirectory,
            in: .userDomainMask
        ).first ?? FileManager.default.temporaryDirectory

        return picturesURL.appendingPathComponent(Brand.appName)
    }

    // MARK: - Private Helpers

    private typealias CGWindowListCreateImageFunction = @convention(c) (
        CGRect,
        UInt32,
        CGWindowID,
        UInt32
    ) -> UnsafeMutableRawPointer?

    /// Captures the given window. Prefer the legacy window-server capture
    /// function when the host still exports it; fall back to AppKit view
    /// caching so the feature degrades without breaking modern SDK builds.
    private func captureWindowImage(_ window: NSWindow) -> CGImage? {
        if let image = captureWindowImageViaWindowServer(window) {
            return image
        }

        return captureWindowImageViaViewCache(window)
    }

    private func captureWindowImageViaWindowServer(_ window: NSWindow) -> CGImage? {
        guard window.windowNumber > 0 else {
            return nil
        }

        let frameworkPath = "/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics"
        guard let handle = dlopen(frameworkPath, RTLD_LAZY) else {
            return nil
        }
        defer { dlclose(handle) }

        guard let symbol = dlsym(handle, "CGWindowListCreateImage") else {
            return nil
        }

        let capture = unsafeBitCast(symbol, to: CGWindowListCreateImageFunction.self)
        let listOption = CGWindowListOption.optionIncludingWindow.rawValue
        let imageOptions = CGWindowImageOption.boundsIgnoreFraming.rawValue |
            CGWindowImageOption.bestResolution.rawValue

        guard let rawImage = capture(CGRect.null, listOption, CGWindowID(window.windowNumber), imageOptions) else {
            return nil
        }

        return Unmanaged<CGImage>.fromOpaque(rawImage).takeRetainedValue()
    }

    private func captureWindowImageViaViewCache(_ window: NSWindow) -> CGImage? {
        guard let contentView = window.contentView else {
            return nil
        }

        let bounds = contentView.bounds
        guard let bitmap = contentView.bitmapImageRepForCachingDisplay(in: bounds) else {
            return nil
        }

        bitmap.size = bounds.size
        contentView.cacheDisplay(in: bounds, to: bitmap)
        return bitmap.cgImage
    }

    /// Saves a CGImage as a PNG file at the given URL.
    ///
    /// - Parameters:
    ///   - image: The image to save.
    ///   - url: The destination file URL.
    /// - Returns: `true` on success, `false` on failure.
    private func savePNG(_ image: CGImage, to url: URL) -> Bool {
        guard let destination = CGImageDestinationCreateWithURL(
            url as CFURL,
            UTType.png.identifier as CFString,
            1,
            nil
        ) else {
            return false
        }

        CGImageDestinationAddImage(destination, image, nil)
        return CGImageDestinationFinalize(destination)
    }

    /// Plays the system screenshot / camera shutter sound.
    private func playScreenshotSound() {
        // "Grab" is the screenshot sound bundled with macOS. Fall back to the
        // generic system "Pop" or "Tink" if unavailable.
        if let sound = NSSound(named: "Grab") {
            sound.play()
        } else if let fallback = NSSound(named: "Pop") {
            fallback.play()
        }
    }

    /// Posts a UNUserNotification announcing the saved screenshot.
    ///
    /// - Parameters:
    ///   - filename: The screenshot filename (used as the notification body).
    ///   - fileURL: The full URL to the saved PNG (used as a notification attachment).
    private func postNotification(filename: String, fileURL: URL) {
        let center = UNUserNotificationCenter.current()

        // Request authorisation if not already granted.
        center.requestAuthorization(options: [.alert, .sound]) { granted, error in
            if let error = error {
                NSLog("[ScreenshotManager] Notification authorisation error: %@",
                      error.localizedDescription)
            }
            guard granted else { return }

            let content = UNMutableNotificationContent()
            content.title = "Screenshot Saved"
            content.body = filename
            content.categoryIdentifier = "SCREENSHOT"

            // Attach the saved image to the notification.
            do {
                let attachment = try UNNotificationAttachment(
                    identifier: "screenshot-image",
                    url: fileURL,
                    options: nil
                )
                content.attachments = [attachment]
            } catch {
                NSLog("[ScreenshotManager] Could not attach image to notification: %@",
                      error.localizedDescription)
            }

            let request = UNNotificationRequest(
                identifier: "screenshot-\(Date().timeIntervalSince1970)",
                content: content,
                trigger: nil // Deliver immediately.
            )

            center.add(request) { error in
                if let error = error {
                    NSLog("[ScreenshotManager] Failed to post notification: %@",
                          error.localizedDescription)
                }
            }
        }

        // Register the "Show in Finder" action category once.
        registerNotificationCategory(center: center, fileURL: fileURL)
    }

    /// Registers the notification category with a "Show in Finder" action.
    private func registerNotificationCategory(center: UNUserNotificationCenter, fileURL: URL) {
        let showAction = UNNotificationAction(
            identifier: "SHOW_IN_FINDER",
            title: "Show in Finder",
            options: .foreground
        )

        let category = UNNotificationCategory(
            identifier: "SCREENSHOT",
            actions: [showAction],
            intentIdentifiers: [],
            options: []
        )

        center.setNotificationCategories([category])
    }

    // MARK: - Flash Animation

    /// Triggers a brief white flash overlay on the window.
    ///
    /// The overlay fades from 0 to 0.3 opacity over 0.1s, then back to 0 over 0.2s.
    /// The overlay is removed after the animation completes.
    ///
    /// - Parameter window: The window to flash.
    private func flashWindow(_ window: NSWindow) {
        guard !isFlashAnimating else { return }
        guard let contentView = window.contentView else { return }

        isFlashAnimating = true

        let overlay = NSView(frame: contentView.bounds)
        overlay.autoresizingMask = [.width, .height]
        overlay.wantsLayer = true
        overlay.layer?.backgroundColor = NSColor.white.cgColor
        overlay.alphaValue = 0.0
        contentView.addSubview(overlay)

        // Phase 1: fade in 0 -> 0.3 over 0.1s.
        NSAnimationContext.runAnimationGroup({ context in
            context.duration = 0.1
            context.timingFunction = CAMediaTimingFunction(name: .easeIn)
            overlay.animator().alphaValue = 0.3
        }, completionHandler: {
            // Phase 2: fade out 0.3 -> 0 over 0.2s.
            NSAnimationContext.runAnimationGroup({ context in
                context.duration = 0.2
                context.timingFunction = CAMediaTimingFunction(name: .easeOut)
                overlay.animator().alphaValue = 0.0
            }, completionHandler: { [weak self] in
                overlay.removeFromSuperview()
                self?.isFlashAnimating = false
            })
        })
    }
}

// MARK: - Notification Delegate Support

/// Extension on AppDelegate to handle screenshot notification actions.
/// The AppDelegate should adopt UNUserNotificationCenterDelegate and set
/// itself as the delegate in applicationDidFinishLaunching.
extension AppDelegate: UNUserNotificationCenterDelegate {

    func userNotificationCenter(
        _ center: UNUserNotificationCenter,
        didReceive response: UNNotificationResponse,
        withCompletionHandler completionHandler: @escaping () -> Void
    ) {
        if response.actionIdentifier == "SHOW_IN_FINDER",
           let attachment = response.notification.request.content.attachments.first {
            let path = attachment.url.path
            NSWorkspace.shared.selectFile(path, inFileViewerRootedAtPath: "")
        }
        completionHandler()
    }

    /// Allow notifications to display even when the app is in the foreground.
    func userNotificationCenter(
        _ center: UNUserNotificationCenter,
        willPresent notification: UNNotification,
        withCompletionHandler completionHandler: @escaping (UNNotificationPresentationOptions) -> Void
    ) {
        completionHandler([.banner, .sound])
    }
}

// MARK: - Menu & Toolbar Integration

/// Menu action and screenshot manager integration on AppDelegate.
extension AppDelegate {

    /// The shared screenshot manager instance. Lazily created on first use.
    private static var _screenshotManager: ScreenshotManager?

    var screenshotManager: ScreenshotManager {
        if let existing = Self._screenshotManager {
            return existing
        }
        let manager = ScreenshotManager()
        Self._screenshotManager = manager
        return manager
    }

    /// Menu action: captures a screenshot of the main game window (Cmd+Shift+S).
    @objc func captureScreenshot(_ sender: Any?) {
        guard let window = NSApp.mainWindow ?? NSApp.windows.first else {
            NSLog("[ScreenshotManager] No window available for screenshot.")
            return
        }
        screenshotManager.captureScreenshot(from: window)
    }
}
