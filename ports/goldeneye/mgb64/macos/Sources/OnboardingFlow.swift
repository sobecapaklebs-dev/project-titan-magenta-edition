/**
 * OnboardingFlow.swift — First-launch onboarding experience for MGB64.
 *
 * A multi-page SwiftUI sheet that walks the user through welcome, ROM setup,
 * controls overview, and a ready screen. Stores a completion flag in
 * UserDefaults so it only appears on first launch (re-triggerable from
 * Help > Show Welcome Guide).
 */

import SwiftUI

// MARK: - UserDefaults key

private let onboardingCompleteKey = "onboardingComplete"

// MARK: - OnboardingFlow

@available(macOS 14.0, *)
struct OnboardingFlow: View {
    /// Dismiss action provided by the sheet presentation.
    @Environment(\.dismiss) private var dismiss

    /// Called when the user finishes onboarding and taps "Play".
    var onFinished: (String?) -> Void

    /// The currently displayed page (0-indexed).
    @State private var currentPage: Int = 0

    /// ROM path captured from the embedded ROMPickerView.
    @State private var validatedROMPath: String?

    /// Whether the ROM was validated (gates auto-advance from page 1 to 2).
    @State private var romValidated: Bool = false

    private let pageCount = 4

    var body: some View {
        VStack(spacing: 0) {
            // Page content
            Group {
                switch currentPage {
                case 0:
                    welcomePage
                case 1:
                    romSetupPage
                case 2:
                    controlsPage
                case 3:
                    readyPage
                default:
                    EmptyView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .animation(.easeInOut(duration: 0.3), value: currentPage)

            Divider()

            // Page indicator dots
            pageIndicator
                .padding(.vertical, 12)
        }
        .frame(width: 700, height: 500)
        .accessibilityElement(children: .contain)
        .accessibilityLabel("\(Brand.appName) onboarding, page \(currentPage + 1) of \(pageCount)")
    }

    // MARK: - Page 1: Welcome

    private var welcomePage: some View {
        VStack(spacing: 20) {
            Spacer()

            Image(systemName: "antenna.radiowaves.left.and.right")
                .font(.system(size: 64))
                .foregroundStyle(.tint)
                .accessibilityHidden(true)

            Text("Welcome to \(Brand.appName)")
                .font(.largeTitle)
                .fontWeight(.bold)
                .multilineTextAlignment(.center)
                .accessibilityAddTraits(.isHeader)

            Text(Brand.tagline)
                .font(.title3)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 40)

            Spacer()

            Button("Get Started") {
                advanceToPage(1)
            }
            .controlSize(.large)
            .keyboardShortcut(.defaultAction)
            .accessibilityLabel("Get started with setup")

            Spacer()
                .frame(height: 24)
        }
        .padding(.horizontal, 40)
    }

    // MARK: - Page 2: ROM Setup

    private var romSetupPage: some View {
        ROMPickerView { path in
            validatedROMPath = path
            romValidated = true
            advanceToPage(2)
        }
        .accessibilityLabel("ROM file selection")
    }

    // MARK: - Page 3: Controls

    private var controlsPage: some View {
        VStack(spacing: 16) {
            Text("Controls")
                .font(.title2)
                .fontWeight(.semibold)
                .accessibilityAddTraits(.isHeader)

            HStack(alignment: .top, spacing: 32) {
                // Keyboard layout
                controlsCard(
                    icon: "keyboard",
                    title: "Keyboard & Mouse",
                    bindings: [
                        ("W A S D", "Move"),
                        ("Mouse", "Look / Aim"),
                        ("Left Click", "Fire"),
                        ("Right Click", "Aim Mode"),
                        ("R", "Reload"),
                        ("E", "Interact / Open"),
                        ("Space", "Accept / Confirm"),
                        ("Shift", "Crouch"),
                        ("Esc", "Pause Menu"),
                    ]
                )

                // Gamepad layout
                controlsCard(
                    icon: "gamecontroller.fill",
                    title: "Gamepad",
                    bindings: [
                        ("Left Stick", "Move"),
                        ("Right Stick", "Look / Aim"),
                        ("RT", "Fire"),
                        ("LT", "Aim Mode"),
                        ("X / Square", "Reload"),
                        ("A / Cross", "Interact / Open"),
                        ("B / Circle", "Back / Cancel"),
                        ("D-Pad", "Weapon Select"),
                        ("Start", "Pause Menu"),
                    ]
                )
            }
            .padding(.horizontal, 24)

            HStack(spacing: 6) {
                Image(systemName: "gamecontroller.fill")
                    .foregroundStyle(.secondary)
                Text("Connect a controller for the best experience")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }
            .accessibilityElement(children: .combine)

            Spacer(minLength: 0)

            Button("Next") {
                advanceToPage(3)
            }
            .controlSize(.large)
            .keyboardShortcut(.defaultAction)
            .accessibilityLabel("Continue to final page")

            Spacer()
                .frame(height: 8)
        }
        .padding(.top, 20)
    }

    // MARK: - Page 4: Ready

    private var readyPage: some View {
        VStack(spacing: 20) {
            Spacer()

            Image(systemName: "checkmark.circle.fill")
                .font(.system(size: 64))
                .foregroundStyle(.green)
                .accessibilityHidden(true)

            Text("You're All Set!")
                .font(.largeTitle)
                .fontWeight(.bold)
                .accessibilityAddTraits(.isHeader)

            Text("Enjoy \(Brand.appName). You can revisit this guide anytime from the Help menu.")
                .font(.body)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal, 60)

            Spacer()

            Button("Play") {
                markOnboardingComplete()
                dismiss()
                onFinished(validatedROMPath)
            }
            .controlSize(.large)
            .keyboardShortcut(.defaultAction)
            .accessibilityLabel("Start playing \(Brand.appName)")

            Spacer()
                .frame(height: 24)
        }
        .padding(.horizontal, 40)
    }

    // MARK: - Shared Components

    /// A card showing a list of control bindings under an icon and title.
    private func controlsCard(
        icon: String,
        title: String,
        bindings: [(String, String)]
    ) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Label(title, systemImage: icon)
                .font(.headline)
                .accessibilityAddTraits(.isHeader)

            Divider()

            ForEach(Array(bindings.enumerated()), id: \.offset) { _, binding in
                HStack {
                    Text(binding.0)
                        .font(.system(.callout, design: .monospaced))
                        .frame(width: 100, alignment: .leading)
                    Text(binding.1)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("\(binding.0): \(binding.1)")
            }
        }
        .padding(16)
        .frame(maxWidth: .infinity, alignment: .leading)
        .background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 10))
    }

    /// Page indicator dots.
    private var pageIndicator: some View {
        HStack(spacing: 8) {
            ForEach(0..<pageCount, id: \.self) { index in
                Circle()
                    .fill(index == currentPage ? Color.accentColor : Color.secondary.opacity(0.3))
                    .frame(width: 8, height: 8)
                    .scaleEffect(index == currentPage ? 1.2 : 1.0)
                    .animation(.easeInOut(duration: 0.2), value: currentPage)
                    .accessibilityLabel("Page \(index + 1) of \(pageCount)")
                    .accessibilityAddTraits(index == currentPage ? .isSelected : [])
            }
        }
    }

    // MARK: - Navigation

    private func advanceToPage(_ page: Int) {
        withAnimation(.easeInOut(duration: 0.3)) {
            currentPage = min(page, pageCount - 1)
        }
    }

    // MARK: - Persistence

    private func markOnboardingComplete() {
        UserDefaults.standard.set(true, forKey: onboardingCompleteKey)
    }

    /// Whether onboarding has been completed previously.
    static var isComplete: Bool {
        UserDefaults.standard.bool(forKey: onboardingCompleteKey)
    }

    /// Resets the onboarding flag so the flow appears again on next launch.
    /// Call from Help > Show Welcome Guide.
    static func resetCompletion() {
        UserDefaults.standard.set(false, forKey: onboardingCompleteKey)
    }
}
