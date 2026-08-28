/**
 * ErrorPresenter.swift — Centralized error handling for the MGB64 macOS app.
 *
 * All user-facing error dialogs go through this enum. Each method presents an
 * NSAlert with buttons appropriate to the failure class (ROM load, engine init,
 * crash recovery, generic) and invokes a completion handler so the caller can
 * react to the user's choice.
 */

import Cocoa

// MARK: - Response types

/// The user's choice after a ROM error alert.
enum ROMErrorResponse {
    case selectDifferentROM
    case quit
}

/// The user's choice after a game engine initialization error alert.
enum InitErrorResponse {
    case reportIssue
    case retry
    case quit
}

/// The user's choice after a recoverable crash alert.
enum CrashRecoveryResponse {
    case continueRunning
    case reportAndQuit
}

/// The user's choice after a generic error alert.
enum GenericErrorResponse {
    case ok
}

// MARK: - ErrorPresenter

@available(macOS 14.0, *)
enum ErrorPresenter {

    // MARK: - ROM Loading Errors

    /// Presents an alert for ROM loading failures.
    ///
    /// - Parameters:
    ///   - message: A description of what went wrong (e.g. "CRC mismatch", "File not found").
    ///   - window: The parent window for the sheet. Pass `nil` to show as a standalone modal.
    ///   - completion: Called with the user's button choice.
    static func showROMError(
        message: String,
        in window: NSWindow?,
        completion: @escaping (ROMErrorResponse) -> Void
    ) {
        let alert = NSAlert()
        alert.alertStyle = .critical
        alert.messageText = "ROM Loading Failed"
        alert.informativeText = """
            \(Brand.appName) could not load the selected ROM file.

            \(message)

            Please select a compatible N64 ROM (.z64, .v64, or .n64).
            """

        alert.addButton(withTitle: "Select Different ROM")
        alert.addButton(withTitle: "Quit")

        presentAlert(alert, in: window, accessibilityLabel: "ROM loading error alert") { response in
            switch response {
            case .alertFirstButtonReturn:
                completion(.selectDifferentROM)
            default:
                completion(.quit)
            }
        }
    }

    // MARK: - Engine Initialization Errors

    /// Presents an alert for game engine initialization failures.
    ///
    /// - Parameters:
    ///   - message: Technical details about the init failure.
    ///   - window: The parent window for the sheet. Pass `nil` to show as a standalone modal.
    ///   - completion: Called with the user's button choice.
    static func showInitError(
        message: String,
        in window: NSWindow?,
        completion: @escaping (InitErrorResponse) -> Void
    ) {
        let alert = NSAlert()
        alert.alertStyle = .critical
        alert.messageText = "Engine Initialization Failed"
        alert.informativeText = """
            \(Brand.appName) encountered an error during startup.

            \(message)

            If this problem persists, please report it so we can investigate.
            """

        // Button order: first = Report Issue, second = Retry, third = Quit
        alert.addButton(withTitle: "Report Issue")
        alert.addButton(withTitle: "Retry")
        alert.addButton(withTitle: "Quit")

        presentAlert(alert, in: window, accessibilityLabel: "Engine initialization error alert") { response in
            switch response {
            case .alertFirstButtonReturn:
                openIssueTracker()
                completion(.reportIssue)
            case .alertSecondButtonReturn:
                completion(.retry)
            default:
                completion(.quit)
            }
        }
    }

    // MARK: - Crash Recovery

    /// Presents an alert for recoverable crashes during gameplay.
    ///
    /// - Parameters:
    ///   - details: Technical information about the crash (stack trace, error code, etc.).
    ///   - window: The parent window for the sheet. Pass `nil` to show as a standalone modal.
    ///   - completion: Called with the user's button choice.
    static func showCrashRecovery(
        details: String,
        in window: NSWindow?,
        completion: @escaping (CrashRecoveryResponse) -> Void
    ) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Something Went Wrong"
        alert.informativeText = """
            \(Brand.appName) encountered an unexpected error but may be able to continue.

            Technical details:
            \(details)
            """

        alert.addButton(withTitle: "Continue")
        alert.addButton(withTitle: "Report & Quit")

        presentAlert(alert, in: window, accessibilityLabel: "Crash recovery alert") { response in
            switch response {
            case .alertFirstButtonReturn:
                completion(.continueRunning)
            default:
                openIssueTracker()
                completion(.reportAndQuit)
            }
        }
    }

    // MARK: - Generic / Catch-All

    /// Presents a catch-all error alert.
    ///
    /// - Parameters:
    ///   - error: The error to display.
    ///   - window: The parent window for the sheet. Pass `nil` to show as a standalone modal.
    ///   - completion: Called when the user dismisses the alert.
    static func showGenericError(
        _ error: Error,
        in window: NSWindow?,
        completion: @escaping (GenericErrorResponse) -> Void = { _ in }
    ) {
        let alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "An Error Occurred"
        alert.informativeText = """
            \(Brand.appName) encountered an error:

            \(error.localizedDescription)
            """

        alert.addButton(withTitle: "OK")

        presentAlert(alert, in: window, accessibilityLabel: "Error alert") { _ in
            completion(.ok)
        }
    }

    // MARK: - Private Helpers

    /// Presents an alert as a sheet on the given window, or as an app-modal dialog if
    /// the window is `nil`.
    private static func presentAlert(
        _ alert: NSAlert,
        in window: NSWindow?,
        accessibilityLabel: String,
        completion: @escaping (NSApplication.ModalResponse) -> Void
    ) {
        alert.window.setAccessibilityLabel(accessibilityLabel)

        if let window = window {
            alert.beginSheetModal(for: window) { response in
                completion(response)
            }
        } else {
            let response = alert.runModal()
            completion(response)
        }
    }

    /// Opens the project's issue tracker in the user's default browser.
    private static func openIssueTracker() {
        NSWorkspace.shared.open(Brand.issuesURL)
    }
}
