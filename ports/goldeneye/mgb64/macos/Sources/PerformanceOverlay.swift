// PerformanceOverlay.swift — Lightweight FPS and frame timing overlay for MGB64.
//
// Draws a small transparent HUD in the top-right corner of the game window
// showing current FPS, frame time in milliseconds, and a rolling mini graph of
// the last 60 frame times. Rendered entirely with Core Graphics for minimal
// overhead — no SwiftUI, no Auto Layout, no heavy frameworks.
//
// The overlay reads game_get_frame_count() (from GameBridge.h) on a lightweight
// UI timer to compute frame timing. It does not intercept any input events, so
// all keyboard/mouse/controller input passes through to the GameRenderView
// underneath.

import Cocoa
import QuartzCore

// MARK: - PerformanceOverlayView

/// A transparent HUD overlay that displays FPS, frame time, and a mini frame
/// time graph. Designed to float above the game render view without capturing
/// any input events.
///
/// Usage:
/// ```swift
/// let overlay = PerformanceOverlayView()
/// window.contentView?.addSubview(overlay)
/// overlay.show()
/// ```
final class PerformanceOverlayView: NSView {

    // MARK: - Constants

    /// Overlay dimensions in points.
    private static let overlayWidth: CGFloat = 180
    private static let overlayHeight: CGFloat = 80

    /// Corner radius for the background pill.
    private static let cornerRadius: CGFloat = 8

    /// Background fill opacity.
    private static let backgroundAlpha: CGFloat = 0.6

    /// Number of frame time samples to retain for the mini graph.
    private static let sampleCount = 60

    /// Size of the mini graph drawing area within the overlay.
    private static let graphWidth: CGFloat = 120
    private static let graphHeight: CGFloat = 30

    /// Margin between the overlay edges and the parent view edges.
    private static let edgeMargin: CGFloat = 12

    // MARK: - State

    /// Circular buffer of recent frame times in seconds.
    private var frameTimes: [Double] = []

    /// Index into `frameTimes` for the next write (circular buffer head).
    private var frameTimeIndex: Int = 0

    /// Whether the circular buffer has been fully populated at least once.
    private var bufferFull: Bool = false

    /// The game frame count at the previous display link callback.
    private var previousFrameCount: UInt64 = 0

    /// The timestamp (in seconds, from CACurrentMediaTime) of the previous callback.
    private var previousTimestamp: Double = 0

    /// Computed FPS value displayed on the overlay.
    private var currentFPS: Double = 0

    /// Computed frame time in milliseconds displayed on the overlay.
    private var currentFrameTimeMs: Double = 0

    /// Main-run-loop timer that drives overlay sampling.
    private var displayTimer: Timer?

    /// Whether the overlay is currently visible.
    private(set) var isVisible: Bool = false

    // MARK: - Fonts

    /// Monospaced font for the large FPS number.
    private let fpsFont: NSFont = {
        if let mono = NSFont(name: "SF Mono Bold", size: 20) {
            return mono
        }
        return NSFont.monospacedSystemFont(ofSize: 20, weight: .bold)
    }()

    /// Monospaced font for the frame time label and graph labels.
    private let detailFont: NSFont = {
        if let mono = NSFont(name: "SF Mono", size: 11) {
            return mono
        }
        return NSFont.monospacedSystemFont(ofSize: 11, weight: .regular)
    }()

    // MARK: - Initialization

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        commonInit()
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
        commonInit()
    }

    private func commonInit() {
        // Pre-fill the frame time buffer with zeros.
        frameTimes = Array(repeating: 0, count: Self.sampleCount)

        // The overlay does not participate in Auto Layout.
        translatesAutoresizingMaskIntoConstraints = true
        autoresizingMask = [.minXMargin, .minYMargin]

        // Start hidden.
        isHidden = true
    }

    deinit {
        stopSamplingTimer()
    }

    // MARK: - Public API

    /// Show the overlay and begin sampling frame times.
    func show() {
        guard !isVisible else { return }
        isVisible = true
        isHidden = false
        positionInSuperview()
        startSamplingTimer()
    }

    /// Hide the overlay and stop sampling.
    func hide() {
        guard isVisible else { return }
        isVisible = false
        isHidden = true
        stopSamplingTimer()
    }

    // MARK: - Positioning

    /// Places the overlay in the top-right corner of its superview.
    private func positionInSuperview() {
        guard let superview = superview else { return }

        let superBounds = superview.bounds
        let x = superBounds.maxX - Self.overlayWidth - Self.edgeMargin
        let y = superBounds.maxY - Self.overlayHeight - Self.edgeMargin
        frame = NSRect(
            x: x,
            y: y,
            width: Self.overlayWidth,
            height: Self.overlayHeight
        )
    }

    override func viewDidMoveToSuperview() {
        super.viewDidMoveToSuperview()
        if isVisible {
            positionInSuperview()
        }
    }

    override func resize(withOldSuperviewSize oldSize: NSSize) {
        super.resize(withOldSuperviewSize: oldSize)
        positionInSuperview()
    }

    // MARK: - Input Pass-Through

    // The overlay must not intercept any events. By returning nil from hitTest,
    // all mouse events fall through to the view underneath (the game render view).

    override func hitTest(_ point: NSPoint) -> NSView? {
        return nil
    }

    override var acceptsFirstResponder: Bool { false }

    // MARK: - Sampling Timer

    /// Creates and starts a lightweight timer to sample frame timing.
    private func startSamplingTimer() {
        guard displayTimer == nil else { return }

        previousFrameCount = game_get_frame_count()
        previousTimestamp = CACurrentMediaTime()

        let timer = Timer(
            timeInterval: 1.0 / 60.0,
            target: self,
            selector: #selector(sampleTimerFired(_:)),
            userInfo: nil,
            repeats: true
        )
        RunLoop.main.add(timer, forMode: .common)
        displayTimer = timer
    }

    /// Stops and releases the sampling timer.
    private func stopSamplingTimer() {
        displayTimer?.invalidate()
        displayTimer = nil
    }

    /// Called by the sampling timer. Reads the game frame count, computes
    /// timing, and schedules a redraw.
    @objc private func sampleTimerFired(_ timer: Timer) {
        let now = CACurrentMediaTime()
        let frameCount = game_get_frame_count()

        let elapsed = now - previousTimestamp

        // Avoid division by zero on the very first callback.
        guard elapsed > 0 else {
            previousTimestamp = now
            previousFrameCount = frameCount
            return
        }

        let frameDelta = frameCount - previousFrameCount

        // Compute FPS from the game's own frame counter for accuracy.
        let fps: Double
        if frameDelta > 0 {
            fps = Double(frameDelta) / elapsed
        } else {
            // No new game frames — use the display link interval as a fallback.
            fps = 1.0 / elapsed
        }

        let frameTimeMs = elapsed * 1000.0

        previousTimestamp = now
        previousFrameCount = frameCount

        // Store the sample in the circular buffer.
        frameTimes[frameTimeIndex] = elapsed
        frameTimeIndex = (frameTimeIndex + 1) % Self.sampleCount
        if frameTimeIndex == 0 {
            bufferFull = true
        }

        currentFPS = fps
        currentFrameTimeMs = frameTimeMs

        needsDisplay = true
    }

    // MARK: - Drawing

    override func draw(_ dirtyRect: NSRect) {
        guard let context = NSGraphicsContext.current?.cgContext else { return }

        let bounds = self.bounds

        // --- Background ---
        let bgPath = CGPath(
            roundedRect: bounds,
            cornerWidth: Self.cornerRadius,
            cornerHeight: Self.cornerRadius,
            transform: nil
        )
        context.addPath(bgPath)
        context.setFillColor(NSColor.black.withAlphaComponent(Self.backgroundAlpha).cgColor)
        context.fillPath()

        // --- Text attributes ---
        let fpsAttributes: [NSAttributedString.Key: Any] = [
            .font: fpsFont,
            .foregroundColor: NSColor.white
        ]
        let detailAttributes: [NSAttributedString.Key: Any] = [
            .font: detailFont,
            .foregroundColor: NSColor(white: 0.85, alpha: 1.0)
        ]

        // --- FPS label (top-left area) ---
        let fpsString = NSAttributedString(
            string: String(format: "%.0f FPS", currentFPS),
            attributes: fpsAttributes
        )
        let fpsOrigin = CGPoint(x: 10, y: bounds.height - 26)
        fpsString.draw(at: fpsOrigin)

        // --- Frame time label (below FPS) ---
        let ftString = NSAttributedString(
            string: String(format: "%.1f ms", currentFrameTimeMs),
            attributes: detailAttributes
        )
        let ftOrigin = CGPoint(x: 10, y: bounds.height - 42)
        ftString.draw(at: ftOrigin)

        // --- Mini graph (bottom area, centered horizontally) ---
        let graphX = (bounds.width - Self.graphWidth) / 2
        let graphY: CGFloat = 6
        let graphRect = CGRect(
            x: graphX,
            y: graphY,
            width: Self.graphWidth,
            height: Self.graphHeight
        )

        drawFrameTimeGraph(in: graphRect, context: context)
    }

    /// Draws a line chart of recent frame times within the given rectangle.
    private func drawFrameTimeGraph(in rect: CGRect, context: CGContext) {
        let sampleCount = Self.sampleCount

        // Determine the range of valid samples.
        let validCount = bufferFull ? sampleCount : frameTimeIndex
        guard validCount > 1 else { return }

        // Build the ordered sample array (oldest first).
        var ordered: [Double] = []
        ordered.reserveCapacity(validCount)

        if bufferFull {
            // Read from the current write index (oldest) wrapping around.
            for i in 0..<sampleCount {
                let idx = (frameTimeIndex + i) % sampleCount
                ordered.append(frameTimes[idx])
            }
        } else {
            ordered = Array(frameTimes[0..<validCount])
        }

        // Determine the vertical scale. Clamp the minimum range to 8ms so the
        // graph does not become a flat line during perfectly steady frame pacing.
        let maxTime = max(ordered.max() ?? 0.016, 0.008)

        // Draw a subtle reference line at 16.67ms (60 FPS target).
        let targetFraction = CGFloat(0.01667 / maxTime)
        let targetY = rect.minY + targetFraction * rect.height
        if targetY >= rect.minY && targetY <= rect.maxY {
            context.setStrokeColor(NSColor(white: 1.0, alpha: 0.2).cgColor)
            context.setLineWidth(0.5)
            context.move(to: CGPoint(x: rect.minX, y: targetY))
            context.addLine(to: CGPoint(x: rect.maxX, y: targetY))
            context.strokePath()
        }

        // Build the line path.
        let stepX = rect.width / CGFloat(sampleCount - 1)

        context.setStrokeColor(NSColor(calibratedRed: 0.29, green: 0.44, blue: 0.65, alpha: 1.0).cgColor)
        context.setLineWidth(1.5)
        context.setLineJoin(.round)

        var started = false
        for (i, sample) in ordered.enumerated() {
            let x = rect.minX + CGFloat(i) * stepX
            let fraction = CGFloat(sample / maxTime)
            let y = rect.minY + min(fraction, 1.0) * rect.height

            if !started {
                context.move(to: CGPoint(x: x, y: y))
                started = true
            } else {
                context.addLine(to: CGPoint(x: x, y: y))
            }
        }
        context.strokePath()
    }
}
