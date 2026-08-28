/**
 * PreferencesView.swift — Settings window (Cmd+,) with five tabs:
 * Video, Audio, Controls, Accessibility, Advanced.
 *
 * All settings are persisted via @AppStorage (UserDefaults) with namespaced
 * keys. Once the config bridge (game_config_get_float / game_config_set_float)
 * is fully wired up, these should read from and write to ge007.ini instead.
 *
 * Requires macOS 14.0+ for Form with LabeledContent and modern Section APIs.
 *
 * TODO: Replace @AppStorage reads/writes with game_config_get/set calls
 *       once the config bridge is complete.
 */

import SwiftUI
import GameController
import CoreAudio
import UniformTypeIdentifiers

// MARK: - Settings Keys

private enum SettingsKey {
    // Video
    static let windowScale      = "video.windowScale"
    static let fullscreen       = "video.fullscreen"
    static let vsync            = "video.vsync"
    static let frameRateCap     = "video.frameRateCap"

    // Audio
    static let masterVolume     = "audio.masterVolume"
    static let mute             = "audio.mute"

    // Controls
    static let mouseSensitivity = "input.mouseSensitivity"
    static let aimSensitivity   = "input.aimSensitivity"
    static let invertY          = "input.invertY"
    static let gamepadLookSpeed = "input.gamepadLookSpeed"
    static let vibration        = "input.vibration"

    // Accessibility
    static let reduceMotion         = "a11y.reduceMotion"
    static let highContrastHUD      = "a11y.highContrastHUD"
    static let largeSubtitles       = "a11y.largeSubtitles"
    static let screenFlashReduction = "a11y.screenFlashReduction"

    // Advanced
    static let performanceOverlay = "advanced.performanceOverlay"
    static let debugLogging       = "advanced.debugLogging"
}

// MARK: - Default Values

private enum Defaults {
    static let windowScale: Int       = 2
    static let fullscreen: Bool       = false
    static let vsync: Bool            = true
    static let frameRateCap: Int      = 60

    static let masterVolume: Double   = 1.0
    static let mute: Bool             = false

    static let mouseSensitivity: Double  = 0.25
    static let aimSensitivity: Double    = 0.12
    static let invertY: Bool             = false
    static let gamepadLookSpeed: Double  = 10.0
    static let vibration: Bool           = true

    static let reduceMotion: Bool         = false
    static let highContrastHUD: Bool      = false
    static let largeSubtitles: Bool       = false
    static let screenFlashReduction: Bool = false

    static let performanceOverlay: Bool = false
    static let debugLogging: Int        = 0  // 0 = Off, 1 = Errors Only, 2 = Verbose
}

// MARK: - Debug Logging Level

private enum DebugLoggingLevel: Int, CaseIterable, Identifiable {
    case off        = 0
    case errorsOnly = 1
    case verbose    = 2

    var id: Int { rawValue }

    var label: String {
        switch self {
        case .off:        return "Off"
        case .errorsOnly: return "Errors Only"
        case .verbose:    return "Verbose"
        }
    }
}

// MARK: - Frame Rate Cap

private enum FrameRateCap: Int, CaseIterable, Identifiable {
    case unlimited = 0
    case thirty    = 30
    case sixty     = 60
    case oneTwenty = 120

    var id: Int { rawValue }

    var label: String {
        switch self {
        case .unlimited: return "Unlimited"
        case .thirty:    return "30"
        case .sixty:     return "60"
        case .oneTwenty: return "120"
        }
    }
}

// MARK: - PreferencesView

@available(macOS 14.0, *)
struct PreferencesView: View {
    var body: some View {
        TabView {
            VideoTab()
                .tabItem {
                    Label("Video", systemImage: "display")
                }

            AudioTab()
                .tabItem {
                    Label("Audio", systemImage: "speaker.wave.2")
                }

            ControlsTab()
                .tabItem {
                    Label("Controls", systemImage: "gamecontroller")
                }

            AccessibilityTab()
                .tabItem {
                    Label("Accessibility", systemImage: "accessibility")
                }

            AdvancedTab()
                .tabItem {
                    Label("Advanced", systemImage: "gearshape.2")
                }
        }
        .frame(width: 520, height: 480)
    }
}

// MARK: - Video Tab

@available(macOS 14.0, *)
private struct VideoTab: View {
    @AppStorage(SettingsKey.windowScale) private var windowScale: Int = Defaults.windowScale
    @AppStorage(SettingsKey.fullscreen) private var fullscreen: Bool = Defaults.fullscreen
    @AppStorage(SettingsKey.vsync) private var vsync: Bool = Defaults.vsync
    @AppStorage(SettingsKey.frameRateCap) private var frameRateCap: Int = Defaults.frameRateCap

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    Picker("", selection: $windowScale) {
                        Text("1x  320\u{00D7}240").tag(1)
                        Text("2x  640\u{00D7}480").tag(2)
                        Text("3x  960\u{00D7}720").tag(3)
                        Text("4x  1280\u{00D7}960").tag(4)
                    }
                    .pickerStyle(.segmented)
                    .labelsHidden()
                    .frame(maxWidth: 360)
                } label: {
                    Label("Window Scale", systemImage: "rectangle.expand.vertical")
                }

                LabeledContent {
                    Toggle("", isOn: $fullscreen)
                        .labelsHidden()
                } label: {
                    Label("Fullscreen", systemImage: "arrow.up.left.and.arrow.down.right")
                }
            } header: {
                Label("Window", systemImage: "macwindow")
            }

            Section {
                LabeledContent {
                    Toggle("", isOn: $vsync)
                        .labelsHidden()
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Label("VSync", systemImage: "arrow.trianglehead.2.clockwise")
                        Text("Synchronizes frame rate with display refresh")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                LabeledContent {
                    Picker("", selection: $frameRateCap) {
                        ForEach(FrameRateCap.allCases) { cap in
                            Text(cap.label).tag(cap.rawValue)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 120)
                } label: {
                    Label("Frame Rate Cap", systemImage: "speedometer")
                }
            } header: {
                Label("Performance", systemImage: "gauge.with.dots.needle.33percent")
            }

            Section {
                LabeledContent {
                    Text(displayResolutionText)
                        .foregroundStyle(.secondary)
                } label: {
                    Label("Resolution", systemImage: "rectangle.dashed")
                }

                LabeledContent {
                    Text(displayRefreshRateText)
                        .foregroundStyle(.secondary)
                } label: {
                    Label("Refresh Rate", systemImage: "clock.arrow.2.circlepath")
                }
            } header: {
                Label("Current Display", systemImage: "display")
            }

            Section {
                resetButton {
                    windowScale = Defaults.windowScale
                    fullscreen = Defaults.fullscreen
                    vsync = Defaults.vsync
                    frameRateCap = Defaults.frameRateCap
                }
            }
        }
        .formStyle(.grouped)
    }

    private var displayResolutionText: String {
        guard let screen = NSScreen.main else { return "No display detected" }
        let w = Int(screen.frame.width * screen.backingScaleFactor)
        let h = Int(screen.frame.height * screen.backingScaleFactor)
        let scale = screen.backingScaleFactor
        return "\(w) \u{00D7} \(h) @ \(String(format: "%.0f", scale))x"
    }

    private var displayRefreshRateText: String {
        guard let screen = NSScreen.main else { return "Unknown" }
        let rate = screen.maximumFramesPerSecond
        return rate > 0 ? "\(rate) Hz" : "Unknown"
    }
}

// MARK: - Audio Tab

@available(macOS 14.0, *)
private struct AudioTab: View {
    @AppStorage(SettingsKey.masterVolume) private var masterVolume: Double = Defaults.masterVolume
    @AppStorage(SettingsKey.mute) private var mute: Bool = Defaults.mute

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    HStack(spacing: 8) {
                        Image(systemName: volumeIconName)
                            .foregroundStyle(.secondary)
                            .frame(width: 20)

                        Slider(value: $masterVolume, in: 0.0...1.0, step: 0.01) {
                            EmptyView()
                        }

                        Text("\(Int(masterVolume * 100))%")
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                            .frame(width: 44, alignment: .trailing)
                    }
                } label: {
                    Label("Master Volume", systemImage: "dial.medium")
                }

                LabeledContent {
                    Toggle("", isOn: $mute)
                        .labelsHidden()
                } label: {
                    Label("Mute", systemImage: "speaker.slash")
                }
            } header: {
                Label("Volume", systemImage: "speaker.wave.2")
            }

            Section {
                LabeledContent {
                    Text(currentAudioDeviceName)
                        .foregroundStyle(.secondary)
                } label: {
                    Label("Output Device", systemImage: "hifispeaker")
                }
            } header: {
                Label("Device", systemImage: "cable.connector.horizontal")
            }

            Section {
                resetButton {
                    masterVolume = Defaults.masterVolume
                    mute = Defaults.mute
                }
            }
        }
        .formStyle(.grouped)
    }

    private var volumeIconName: String {
        if mute || masterVolume == 0 {
            return "speaker.slash"
        } else if masterVolume < 0.33 {
            return "speaker.wave.1"
        } else if masterVolume < 0.66 {
            return "speaker.wave.2"
        } else {
            return "speaker.wave.3"
        }
    }

    private var currentAudioDeviceName: String {
        // Attempt to read the default output device name via CoreAudio.
        var deviceID = AudioObjectID(0)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDefaultOutputDevice,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )

        let status = AudioObjectGetPropertyData(
            AudioObjectID(kAudioObjectSystemObject),
            &address,
            0, nil,
            &size,
            &deviceID
        )

        guard status == noErr, deviceID != kAudioObjectUnknown else {
            return "Default system output"
        }

        // Get the device name.
        var nameAddress = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyDeviceNameCFString,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var nameRef: Unmanaged<CFString>?
        var nameSize = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)

        let nameStatus = AudioObjectGetPropertyData(
            deviceID,
            &nameAddress,
            0, nil,
            &nameSize,
            &nameRef
        )

        guard nameStatus == noErr else {
            return "Default system output"
        }

        guard let nameRef = nameRef else {
            return "Default system output"
        }

        return nameRef.takeUnretainedValue() as String
    }
}

// MARK: - Controls Tab

@available(macOS 14.0, *)
private struct ControlsTab: View {
    @AppStorage(SettingsKey.mouseSensitivity) private var mouseSensitivity: Double = Defaults.mouseSensitivity
    @AppStorage(SettingsKey.aimSensitivity) private var aimSensitivity: Double = Defaults.aimSensitivity
    @AppStorage(SettingsKey.invertY) private var invertY: Bool = Defaults.invertY
    @AppStorage(SettingsKey.gamepadLookSpeed) private var gamepadLookSpeed: Double = Defaults.gamepadLookSpeed
    @AppStorage(SettingsKey.vibration) private var vibration: Bool = Defaults.vibration

    @State private var connectedControllerName: String = "None"

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    HStack(spacing: 8) {
                        Slider(value: $mouseSensitivity, in: 0.01...1.0, step: 0.01)
                        Text(String(format: "%.2f", mouseSensitivity))
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                            .frame(width: 40, alignment: .trailing)
                    }
                } label: {
                    Label("Mouse Sensitivity", systemImage: "computermouse")
                }

                LabeledContent {
                    HStack(spacing: 8) {
                        Slider(value: $aimSensitivity, in: 0.01...0.5, step: 0.01)
                        Text(String(format: "%.2f", aimSensitivity))
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                            .frame(width: 40, alignment: .trailing)
                    }
                } label: {
                    Label("Aim Sensitivity", systemImage: "target")
                }

                LabeledContent {
                    Toggle("", isOn: $invertY)
                        .labelsHidden()
                } label: {
                    Label("Invert Y Axis", systemImage: "arrow.up.arrow.down")
                }
            } header: {
                Label("Mouse & Keyboard", systemImage: "keyboard")
            }

            Section {
                LabeledContent {
                    HStack(spacing: 8) {
                        Slider(value: $gamepadLookSpeed, in: 1...20, step: 1)
                        Text("\(Int(gamepadLookSpeed))")
                            .monospacedDigit()
                            .foregroundStyle(.secondary)
                            .frame(width: 24, alignment: .trailing)
                    }
                } label: {
                    Label("Look Speed", systemImage: "dpad")
                }

                LabeledContent {
                    Toggle("", isOn: $vibration)
                        .labelsHidden()
                } label: {
                    Label("Vibration", systemImage: "waveform.path")
                }

                LabeledContent {
                    Text(connectedControllerName)
                        .foregroundStyle(.secondary)
                } label: {
                    Label("Connected Controller", systemImage: "gamecontroller")
                }
            } header: {
                Label("Gamepad", systemImage: "gamecontroller")
            }

            Section {
                resetButton {
                    mouseSensitivity = Defaults.mouseSensitivity
                    aimSensitivity = Defaults.aimSensitivity
                    invertY = Defaults.invertY
                    gamepadLookSpeed = Defaults.gamepadLookSpeed
                    vibration = Defaults.vibration
                }
            }
        }
        .formStyle(.grouped)
        .onAppear {
            refreshControllerName()
        }
        .onReceive(
            NotificationCenter.default.publisher(for: .GCControllerDidConnect)
                .merge(with: NotificationCenter.default.publisher(for: .GCControllerDidDisconnect))
        ) { _ in
            refreshControllerName()
        }
    }

    private func refreshControllerName() {
        if let controller = GCController.controllers().first {
            connectedControllerName = controller.vendorName ?? "Unknown Controller"
        } else {
            connectedControllerName = "None"
        }
    }
}

// MARK: - Accessibility Tab

@available(macOS 14.0, *)
private struct AccessibilityTab: View {
    @AppStorage(SettingsKey.reduceMotion) private var reduceMotion: Bool = Defaults.reduceMotion
    @AppStorage(SettingsKey.highContrastHUD) private var highContrastHUD: Bool = Defaults.highContrastHUD
    @AppStorage(SettingsKey.largeSubtitles) private var largeSubtitles: Bool = Defaults.largeSubtitles
    @AppStorage(SettingsKey.screenFlashReduction) private var screenFlashReduction: Bool = Defaults.screenFlashReduction

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    Toggle("", isOn: $reduceMotion)
                        .labelsHidden()
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Label("Reduce Motion", systemImage: "figure.walk.motion")
                        Text("Reduces UI transition animations")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                LabeledContent {
                    Toggle("", isOn: $highContrastHUD)
                        .labelsHidden()
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Label("High Contrast HUD", systemImage: "circle.lefthalf.filled")
                        Text("Increases contrast of HUD elements")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                LabeledContent {
                    Toggle("", isOn: $largeSubtitles)
                        .labelsHidden()
                } label: {
                    Label("Large Subtitles", systemImage: "captions.bubble")
                }

                LabeledContent {
                    Toggle("", isOn: $screenFlashReduction)
                        .labelsHidden()
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Label("Screen Flash Reduction", systemImage: "lightbulb.slash")
                        Text("Dims bright screen flashes")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }
            } header: {
                Label("Options", systemImage: "accessibility")
            }

            Section {
                Text("These settings affect the \(Brand.appName) interface. In-game accessibility depends on the original game.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            Section {
                resetButton {
                    reduceMotion = Defaults.reduceMotion
                    highContrastHUD = Defaults.highContrastHUD
                    largeSubtitles = Defaults.largeSubtitles
                    screenFlashReduction = Defaults.screenFlashReduction
                }
            }
        }
        .formStyle(.grouped)
    }
}

// MARK: - Advanced Tab

@available(macOS 14.0, *)
private struct AdvancedTab: View {
    @AppStorage("romPath") private var romPath: String = ""
    @AppStorage(SettingsKey.performanceOverlay) private var performanceOverlay: Bool = Defaults.performanceOverlay
    @AppStorage(SettingsKey.debugLogging) private var debugLogging: Int = Defaults.debugLogging

    var body: some View {
        Form {
            Section {
                LabeledContent {
                    HStack(spacing: 8) {
                        Text(romPath.isEmpty ? "Not set" : abbreviatedPath(romPath))
                            .foregroundStyle(romPath.isEmpty ? .tertiary : .secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                            .help(romPath.isEmpty ? "No ROM selected" : romPath)

                        Button("Change\u{2026}") {
                            chooseROMFile()
                        }
                        .controlSize(.small)
                    }
                } label: {
                    Label("ROM Path", systemImage: "doc.badge.gearshape")
                }
            } header: {
                Label("Game", systemImage: "opticaldisc")
            }

            Section {
                LabeledContent {
                    Toggle("", isOn: $performanceOverlay)
                        .labelsHidden()
                } label: {
                    VStack(alignment: .leading, spacing: 2) {
                        Label("Performance Overlay", systemImage: "speedometer")
                        Text("Shows an FPS counter during gameplay")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                    }
                }

                LabeledContent {
                    Picker("", selection: $debugLogging) {
                        ForEach(DebugLoggingLevel.allCases) { level in
                            Text(level.label).tag(level.rawValue)
                        }
                    }
                    .labelsHidden()
                    .frame(width: 140)
                } label: {
                    Label("Debug Logging", systemImage: "text.alignleft")
                }
            } header: {
                Label("Diagnostics", systemImage: "waveform.path.ecg")
            }

            Section {
                LabeledContent {
                    HStack(spacing: 8) {
                        Text(abbreviatedPath(saveDirectoryPath))
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                            .help(saveDirectoryPath)

                        Button("Reveal in Finder") {
                            NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: saveDirectoryPath)
                        }
                        .controlSize(.small)
                    }
                } label: {
                    Label("Save Directory", systemImage: "folder")
                }
            } header: {
                Label("Storage", systemImage: "externaldrive")
            }

            Section {
                LabeledContent {
                    Text("\(Brand.version) (\(Brand.buildNumber))")
                        .foregroundStyle(.secondary)
                } label: {
                    Label(Brand.appName, systemImage: "info.circle")
                }
            } header: {
                Label("About", systemImage: "star")
            }

            Section {
                resetButton {
                    performanceOverlay = Defaults.performanceOverlay
                    debugLogging = Defaults.debugLogging
                }
            }
        }
        .formStyle(.grouped)
    }

    private var saveDirectoryPath: String {
        guard let appSupport = FileManager.default.urls(
            for: .applicationSupportDirectory,
            in: .userDomainMask
        ).first else {
            return "~/Library/Application Support/\(Brand.appSupportDirectoryName)"
        }
        return appSupport
            .appendingPathComponent(Brand.appSupportDirectoryName)
            .path
    }

    private func chooseROMFile() {
        let panel = NSOpenPanel()
        panel.title = "Select ROM File"
        panel.allowedContentTypes = [.data]
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false

        if panel.runModal() == .OK, let url = panel.url {
            romPath = url.path
        }
    }
}

// MARK: - Shared Helpers

/// Abbreviates a file path for display by collapsing the home directory to ~.
private func abbreviatedPath(_ path: String) -> String {
    let home = FileManager.default.homeDirectoryForCurrentUser.path
    if path.hasPrefix(home) {
        return "~" + path.dropFirst(home.count)
    }
    return path
}

/// A standardized "Reset to Defaults" button used at the bottom of every settings tab.
@available(macOS 14.0, *)
private func resetButton(action: @escaping () -> Void) -> some View {
    HStack {
        Spacer()
        Button(role: .destructive) {
            action()
        } label: {
            Label("Reset to Defaults", systemImage: "arrow.counterclockwise")
        }
        .controlSize(.small)
        .buttonStyle(.borderless)
        Spacer()
    }
}
