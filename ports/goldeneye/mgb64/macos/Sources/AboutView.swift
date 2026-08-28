/**
 * AboutView.swift — Custom About window for MGB64.
 *
 * All user-facing strings come from the Brand enum in BrandConfig.swift.
 * Presented as a non-resizable panel via `AboutView.show()`.
 */

import SwiftUI
import AppKit

@available(macOS 14.0, *)
struct AboutView: View {
    @Environment(\.openURL) private var openURL

    var body: some View {
        VStack(spacing: 16) {
            Spacer()
                .frame(height: 8)

            // App icon motif
            Image(systemName: "antenna.radiowaves.left.and.right")
                .font(.system(size: 64))
                .foregroundStyle(Color(hex: Brand.Colors.primary))
                .accessibilityLabel("\(Brand.appName) icon")

            // App name
            Text(Brand.appName)
                .font(.largeTitle)
                .bold()
                .foregroundStyle(.white)

            // Version and build
            Text("Version \(Brand.version) (\(Brand.buildNumber))")
                .font(.caption)
                .foregroundStyle(.secondary)

            // Tagline
            Text(Brand.tagline)
                .font(.body)
                .foregroundStyle(.white)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 24)

            // Honest work-in-progress status
            Text(Brand.statusNote)
                .font(.caption)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 24)

            Divider()
                .padding(.horizontal, 32)

            // Disclaimer
            Text(Brand.disclaimer)
                .font(.caption2)
                .foregroundStyle(.tertiary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 24)

            // Action buttons
            HStack(spacing: 16) {
                Button {
                    openURL(Brand.repositoryURL)
                } label: {
                    Label("View Source", systemImage: "chevron.left.forwardslash.chevron.right")
                }
                .accessibilityLabel("View source code on GitHub")

                Button {
                    openURL(Brand.issuesURL)
                } label: {
                    Label("Report Issue", systemImage: "exclamationmark.bubble")
                }
                .accessibilityLabel("Report an issue on GitHub")
            }
            .buttonStyle(.bordered)

            Spacer()
                .frame(height: 8)
        }
        .frame(width: 400, height: 380)
        .background(
            LinearGradient(
                colors: [
                    Color(hex: Brand.Colors.background),
                    Color(hex: Brand.Colors.background).opacity(0.85)
                ],
                startPoint: .top,
                endPoint: .bottom
            )
        )
    }

    // MARK: - Presentation

    /// Creates and shows the About window as a floating panel.
    static func show() {
        // If an existing About panel is open, bring it forward instead of creating a duplicate.
        for window in NSApp.windows where window.identifier == aboutWindowIdentifier {
            window.makeKeyAndOrderFront(nil)
            return
        }

        let hostingView = NSHostingController(rootView: AboutView())

        let panel = NSPanel(
            contentRect: NSRect(x: 0, y: 0, width: 400, height: 380),
            styleMask: [.titled, .closable, .nonactivatingPanel],
            backing: .buffered,
            defer: false
        )
        panel.identifier = aboutWindowIdentifier
        panel.title = "About \(Brand.appName)"
        panel.contentViewController = hostingView
        panel.isReleasedWhenClosed = false
        panel.center()

        // Prevent resizing
        panel.minSize = NSSize(width: 400, height: 380)
        panel.maxSize = NSSize(width: 400, height: 380)

        panel.makeKeyAndOrderFront(nil)
    }

    private static let aboutWindowIdentifier = NSUserInterfaceItemIdentifier("about-panel")
}
