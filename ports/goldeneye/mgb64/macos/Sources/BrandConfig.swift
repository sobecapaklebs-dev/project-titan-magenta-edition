/**
 * BrandConfig.swift — Single source of truth for all user-facing brand strings.
 *
 * The app name, bundle ID, and all branded copy live HERE and nowhere else.
 * All UI code references these constants. To rename the app, change this file
 * and the Info.plist CFBundleName — nothing else.
 *
 * Internal code (GameBridge.c, the port, build scripts) uses "ge007" or
 * generic identifiers. The brand name is a UI-layer concern only.
 */

import Foundation

enum Brand {
    /// The full public-facing app name.
    /// Used in About panel, onboarding, and wherever the full name fits.
    static let appName = "The Man with the Golden Build"

    /// Short form for compact UI: window title, menu bar, dock, directories.
    static let shortName = "MGB64"

    /// The bundle identifier for code signing and system registration.
    static let bundleIdentifier = "com.mgb64.app"

    /// One-line app description for About panel and marketing.
    static let tagline = "An N64 game engine ported natively to macOS, built on a faithful decompilation."

    /// Honest status note for the About panel (work-in-progress framing).
    static let statusNote = "Work in progress \u{2014} a community-iteration port, not a 1:1 replacement for original hardware. See PORT.md for known limitations."

    /// Version string (mirrors Info.plist CFBundleShortVersionString).
    static let version: String = {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0.1.0"
    }()

    /// Build number (mirrors Info.plist CFBundleVersion).
    static let buildNumber: String = {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? "1"
    }()

    /// Legal disclaimer for About panel and README.
    static let disclaimer = """
        Not affiliated with Nintendo, Rare, Microsoft, \
        MGM, Danjaq LLC, or EON Productions.
        """

    /// Copyright line for About panel.
    static let copyright = "Copyright \u{00A9} 2026. \(disclaimer)"

    /// The Application Support subdirectory name.
    /// Uses the short name: ~/Library/Application Support/MGB64/
    static let appSupportDirectoryName = "MGB64"

    /// UserDefaults suite name (nil = standard defaults).
    /// Using nil keeps things simple; keys are namespaced by the bundle ID.
    static let defaultsSuite: String? = nil

    // MARK: - URLs

    /// GitHub repository URL.
    static let repositoryURL = URL(string: "https://github.com/akratch/mgb64")!

    /// Issue tracker URL.
    static let issuesURL = URL(string: "https://github.com/akratch/mgb64/issues")!

    /// Project website URL (points to the repository until a site exists).
    static let websiteURL = URL(string: "https://github.com/akratch/mgb64")!

    // MARK: - Design Tokens

    enum Colors {
        /// Steel blue — primary brand color. Buttons, links, active states.
        static let primary = "#4A6FA5"
        /// Amber gold — accent highlights, progress indicators.
        static let accent = "#D4A843"
        /// Dark background for splash/overlay panels.
        static let background = "#1C1C1E"
    }
}
