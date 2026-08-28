/**
 * SplashView.swift — Loading overlay shown while the game engine initializes.
 *
 * All user-facing strings come from the Brand enum in BrandConfig.swift.
 * Game state is polled via game_is_initialized() from GameBridge.h.
 *
 * Usage: Apply the `.splashOverlay()` modifier to your game content view.
 */

import SwiftUI

// MARK: - View Model

/// Observable model that polls game_is_initialized() and drives splash state.
@available(macOS 14.0, *)
@Observable
final class SplashViewModel {
    enum Status: String {
        case loadingROM = "Loading ROM..."
        case initializingEngine = "Initializing engine..."
        case ready = "Ready"
    }

    private(set) var status: Status = .loadingROM
    private(set) var isInitialized = false

    private var pollTimer: Timer?
    private var elapsedPolls: Int = 0

    /// Begin polling game_is_initialized() on a 0.25s interval.
    func startPolling() {
        guard pollTimer == nil else { return }
        elapsedPolls = 0

        pollTimer = Timer.scheduledTimer(withTimeInterval: 0.25, repeats: true) { [weak self] timer in
            guard let self else {
                timer.invalidate()
                return
            }

            self.elapsedPolls += 1

            // Transition status text to give the user progress feedback.
            // After ~1s of polling, show "Initializing engine...".
            if self.elapsedPolls >= 4 && self.status == .loadingROM {
                self.status = .initializingEngine
            }

            if game_is_initialized() {
                self.status = .ready
                self.isInitialized = true
                timer.invalidate()
                self.pollTimer = nil
            }
        }
    }

    /// Stop polling (e.g., if the view is torn down before initialization completes).
    func stopPolling() {
        pollTimer?.invalidate()
        pollTimer = nil
    }
}

// MARK: - Splash View

@available(macOS 14.0, *)
struct SplashView: View {
    let viewModel: SplashViewModel

    /// Controls the fade-out when the game is initialized.
    @State private var overlayOpacity: Double = 1.0

    /// Drives the pulsing icon animation.
    @State private var isPulsing = false

    var body: some View {
        ZStack {
            // Dark overlay background
            Color(hex: Brand.Colors.background)
                .opacity(0.95)

            VStack(spacing: 20) {
                // Pulsing antenna icon
                Image(systemName: "antenna.radiowaves.left.and.right")
                    .font(.system(size: 56))
                    .foregroundStyle(Color(hex: Brand.Colors.primary))
                    .scaleEffect(isPulsing ? 1.08 : 0.95)
                    .opacity(isPulsing ? 1.0 : 0.7)
                    .animation(
                        .easeInOut(duration: 1.0).repeatForever(autoreverses: true),
                        value: isPulsing
                    )
                    .accessibilityLabel("\(Brand.appName) loading icon")

                // App name
                Text(Brand.appName)
                    .font(.title)
                    .bold()
                    .foregroundStyle(.white)

                // Tagline
                Text(Brand.tagline)
                    .font(.subheadline)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 32)

                // Native circular progress indicator
                ProgressView()
                    .progressViewStyle(.circular)
                    .controlSize(.regular)
                    .tint(Color(hex: Brand.Colors.accent))

                // Status text
                Text(viewModel.status.rawValue)
                    .font(.callout)
                    .foregroundStyle(Color(hex: Brand.Colors.accent))
                    .contentTransition(.numericText())
                    .animation(.easeInOut(duration: 0.3), value: viewModel.status)
            }
        }
        .ignoresSafeArea()
        .opacity(overlayOpacity)
        .onChange(of: viewModel.isInitialized) { _, initialized in
            if initialized {
                withAnimation(.easeOut(duration: 0.5)) {
                    overlayOpacity = 0.0
                }
            }
        }
        .onAppear {
            isPulsing = true
            viewModel.startPolling()
        }
        .onDisappear {
            viewModel.stopPolling()
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(Brand.appName) is loading. \(viewModel.status.rawValue)")
    }
}

// MARK: - View Modifier

/// Composites the splash overlay on top of the modified view.
@available(macOS 14.0, *)
struct SplashOverlayModifier: ViewModifier {
    let viewModel: SplashViewModel

    /// Tracks whether the overlay should still be in the view tree.
    @State private var showOverlay = true

    func body(content: Content) -> some View {
        content
            .overlay {
                if showOverlay {
                    SplashView(viewModel: viewModel)
                        .onChange(of: viewModel.isInitialized) { _, initialized in
                            if initialized {
                                // Remove from the view tree after the fade-out completes.
                                DispatchQueue.main.asyncAfter(deadline: .now() + 0.6) {
                                    showOverlay = false
                                }
                            }
                        }
                }
            }
    }
}

@available(macOS 14.0, *)
extension View {
    /// Adds a loading splash overlay that automatically dismisses when
    /// `game_is_initialized()` returns true.
    func splashOverlay(viewModel: SplashViewModel) -> some View {
        modifier(SplashOverlayModifier(viewModel: viewModel))
    }
}