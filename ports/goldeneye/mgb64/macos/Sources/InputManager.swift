// InputManager.swift — Unified input manager for GCController gamepad input.
//
// Handles gamepad discovery, connection/disconnection events, button/stick
// mapping from modern controllers to the N64 layout, and optional DualSense
// haptics and adaptive trigger support. Input state is pushed to the game
// engine each frame via the game_set_input() C bridge function.

import Cocoa
import GameController
import CoreHaptics

// MARK: - InputManager

/// Manages gamepad input using the GameController framework, mapping physical
/// controller inputs to the N64 button layout expected by the game engine.
///
/// The manager uses event-driven input via `valueChangedHandler` rather than
/// polling. Each button/stick change immediately updates the internal
/// `GameInputState` accumulator, which is then sent to the game engine.
///
/// DualSense-specific features (haptic feedback and adaptive triggers) are
/// supported when a compatible controller is connected.
final class InputManager {

    // MARK: - Properties

    /// All currently connected game controllers.
    private(set) var connectedControllers: [GCController] = []

    /// The display name of the active (most recently connected) controller,
    /// or `nil` if no controller is connected.
    private(set) var currentControllerName: String?

    /// The accumulated input state, updated by controller event handlers and
    /// sent to the game engine via `game_set_input()`.
    private var inputState = GameInputState()

    /// The active controller used for input. Defaults to the most recently
    /// connected controller.
    private var activeController: GCController?

    /// CoreHaptics engine for DualSense haptic feedback, created lazily when
    /// a compatible controller is connected.
    private var hapticEngine: CHHapticEngine?

    // MARK: - N64 Analog Stick Range

    /// Maximum absolute value for the N64 analog stick axes.
    private static let stickRange: Float = 80.0

    // MARK: - Setup

    /// Registers for controller connection/disconnection notifications and
    /// begins wireless controller discovery.
    ///
    /// Call this once during application launch, after the main window is
    /// created.
    func setup() {
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(controllerDidConnect(_:)),
            name: .GCControllerDidConnect,
            object: nil
        )

        NotificationCenter.default.addObserver(
            self,
            selector: #selector(controllerDidDisconnect(_:)),
            name: .GCControllerDidDisconnect,
            object: nil
        )

        GCController.startWirelessControllerDiscovery {
            // Discovery completion — no action needed. Controllers that are
            // found will trigger GCControllerDidConnect notifications.
        }

        // Pick up any controllers that were already connected before setup().
        for controller in GCController.controllers() {
            addController(controller)
        }
    }

    // MARK: - Controller Connection

    @objc private func controllerDidConnect(_ notification: Notification) {
        guard let controller = notification.object as? GCController else { return }
        addController(controller)
    }

    @objc private func controllerDidDisconnect(_ notification: Notification) {
        guard let controller = notification.object as? GCController else { return }
        removeController(controller)
    }

    /// Registers a newly connected controller and configures its input handlers.
    private func addController(_ controller: GCController) {
        guard !connectedControllers.contains(where: { $0 === controller }) else { return }

        connectedControllers.append(controller)
        controller.playerIndex = .indexUnset

        NSLog("[InputManager] Controller connected: %@", controller.vendorName ?? "Unknown")

        // Make this the active controller.
        activeController = controller
        currentControllerName = controller.vendorName

        configureExtendedGamepad(for: controller)
        configureHaptics(for: controller)
    }

    /// Removes a disconnected controller and clears state if it was the active one.
    private func removeController(_ controller: GCController) {
        connectedControllers.removeAll { $0 === controller }

        NSLog("[InputManager] Controller disconnected: %@", controller.vendorName ?? "Unknown")

        if activeController === controller {
            // Fall back to the next available controller, if any.
            activeController = connectedControllers.first
            currentControllerName = activeController?.vendorName

            if activeController == nil {
                // No controllers remain; zero out the input state.
                inputState = GameInputState()
                sendInputState()
            } else if let fallback = activeController {
                configureExtendedGamepad(for: fallback)
                configureHaptics(for: fallback)
            }
        }
    }

    // MARK: - Gamepad Configuration

    /// Configures the extended gamepad profile's `valueChangedHandler` to map
    /// physical inputs to the N64 controller layout.
    ///
    /// This uses an event-driven model: each physical input change triggers the
    /// handler, which updates `inputState` and pushes it to the engine.
    private func configureExtendedGamepad(for controller: GCController) {
        guard let gamepad = controller.extendedGamepad else {
            NSLog("[InputManager] Controller does not support extended gamepad profile: %@",
                  controller.vendorName ?? "Unknown")
            return
        }

        gamepad.valueChangedHandler = { [weak self] (gamepad, element) in
            guard let self = self else { return }
            self.handleGamepadChange(gamepad)
        }
    }

    /// Reads the full gamepad state and updates `inputState`. Called whenever any
    /// element changes via the `valueChangedHandler`.
    private func handleGamepadChange(_ gamepad: GCExtendedGamepad) {
        var buttons: UInt16 = 0

        // Face buttons
        if gamepad.buttonA.isPressed       { buttons |= UInt16(GAME_BTN_A) }
        if gamepad.buttonB.isPressed       { buttons |= UInt16(GAME_BTN_B) }
        if gamepad.buttonX.isPressed       { buttons |= UInt16(GAME_BTN_B) }  // X → B (secondary)
        if gamepad.buttonY.isPressed       { buttons |= UInt16(GAME_BTN_A) }  // Y → weapon swap (A + custom)

        // Triggers and shoulders
        if gamepad.rightTrigger.isPressed   { buttons |= UInt16(GAME_BTN_Z) }  // RT → Z-trigger
        if gamepad.leftShoulder.isPressed   { buttons |= UInt16(GAME_BTN_L) }
        if gamepad.rightShoulder.isPressed  { buttons |= UInt16(GAME_BTN_R) }

        // D-pad
        if gamepad.dpad.up.isPressed       { buttons |= UInt16(GAME_BTN_DU) }
        if gamepad.dpad.down.isPressed     { buttons |= UInt16(GAME_BTN_DD) }
        if gamepad.dpad.left.isPressed     { buttons |= UInt16(GAME_BTN_DL) }
        if gamepad.dpad.right.isPressed    { buttons |= UInt16(GAME_BTN_DR) }

        // Menu / Start
        if gamepad.buttonMenu.isPressed    { buttons |= UInt16(GAME_BTN_START) }

        inputState.buttons = buttons

        // Left stick → N64 analog stick (scale -1..1 to -80..80)
        let lx = gamepad.leftThumbstick.xAxis.value
        let ly = gamepad.leftThumbstick.yAxis.value
        inputState.stick_x = clampToStickRange(lx)
        inputState.stick_y = clampToStickRange(ly)

        // Right stick → right_stick_x/y (passed through as -1..1 float)
        inputState.right_stick_x = gamepad.rightThumbstick.xAxis.value
        inputState.right_stick_y = gamepad.rightThumbstick.yAxis.value

        sendInputState()
    }

    /// Clamps a -1..1 float axis value to the N64 stick range of -80..80 and
    /// returns it as an `Int8`.
    private func clampToStickRange(_ value: Float) -> Int8 {
        let scaled = value * Self.stickRange
        let clamped = max(-Self.stickRange, min(Self.stickRange, scaled))
        return Int8(clamped)
    }

    /// Sends the current accumulated input state to the game engine.
    private func sendInputState() {
        withUnsafePointer(to: inputState) { ptr in
            game_set_input(ptr)
        }
    }

    // MARK: - DualSense Haptics

    /// Configures CoreHaptics for DualSense controllers that support haptic feedback.
    private func configureHaptics(for controller: GCController) {
        // Dispose of any previous engine.
        hapticEngine?.stop(completionHandler: nil)
        hapticEngine = nil

        guard let haptics = controller.haptics else {
            NSLog("[InputManager] Controller does not support haptics: %@",
                  controller.vendorName ?? "Unknown")
            return
        }

        guard let engine = haptics.createEngine(withLocality: .default) else {
            NSLog("[InputManager] Failed to create haptic engine for: %@",
                  controller.vendorName ?? "Unknown")
            return
        }

        engine.stoppedHandler = { [weak self] reason in
            NSLog("[InputManager] Haptic engine stopped: %@", String(describing: reason))
            self?.hapticEngine = nil
        }

        engine.resetHandler = { [weak engine] in
            guard let engine = engine else { return }
            do {
                try engine.start()
            } catch {
                NSLog("[InputManager] Failed to restart haptic engine: %@", error.localizedDescription)
            }
        }

        do {
            try engine.start()
            hapticEngine = engine
            NSLog("[InputManager] Haptic engine started for: %@", controller.vendorName ?? "Unknown")
        } catch {
            NSLog("[InputManager] Failed to start haptic engine: %@", error.localizedDescription)
        }
    }

    /// Triggers a haptic feedback pulse on the active controller.
    ///
    /// On DualSense controllers this produces precise rumble via CoreHaptics.
    /// On controllers without haptic support, this is a no-op.
    ///
    /// - Parameters:
    ///   - intensity: Haptic intensity from 0.0 (none) to 1.0 (maximum).
    ///   - duration: Duration of the haptic pulse in seconds.
    func triggerHaptic(intensity: Float, duration: Float) {
        guard let engine = hapticEngine else { return }

        let clampedIntensity = max(0.0, min(1.0, intensity))
        let clampedDuration = max(0.0, Double(duration))

        let hapticEvent = CHHapticEvent(
            eventType: .hapticTransient,
            parameters: [
                CHHapticEventParameter(parameterID: .hapticIntensity, value: clampedIntensity),
                CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.5)
            ],
            relativeTime: 0,
            duration: clampedDuration
        )

        do {
            let pattern = try CHHapticPattern(events: [hapticEvent], parameters: [])
            let player = try engine.makePlayer(with: pattern)
            try player.start(atTime: CHHapticTimeImmediate)
        } catch {
            NSLog("[InputManager] Failed to play haptic: %@", error.localizedDescription)
        }
    }

    /// Configures adaptive trigger resistance on the active DualSense controller.
    ///
    /// This simulates weapon trigger feel by applying resistance to the right
    /// trigger. On controllers that do not support adaptive triggers, this is
    /// a no-op.
    ///
    /// - Parameter strength: Resistance strength from 0.0 (no resistance) to
    ///   1.0 (maximum resistance).
    func setTriggerResistance(strength: Float) {
        guard let controller = activeController else { return }

        // Adaptive triggers are accessed via the GCDualSenseGamepad profile,
        // available on DualSense controllers only.
        guard let dualSense = controller.physicalInputProfile as? GCDualSenseGamepad else {
            return
        }

        let clampedStrength = max(0.0, min(1.0, strength))
        let rightTrigger = dualSense.rightTrigger

        if clampedStrength <= 0 {
            rightTrigger.setModeOff()
            return
        }

        // Apply feedback resistance to the right trigger (weapon trigger).
        var strengths = GCDualSenseAdaptiveTrigger.PositionalResistiveStrengths()
        withUnsafeMutableBytes(of: &strengths.values) { rawValues in
            let values = rawValues.bindMemory(to: Float.self)
            for index in values.indices {
                values[index] = clampedStrength
            }
        }

        rightTrigger.setModeFeedback(resistiveStrengths: strengths)
    }

    // MARK: - Cleanup

    /// Stops wireless discovery, removes notification observers, and shuts down
    /// the haptic engine.
    func teardown() {
        GCController.stopWirelessControllerDiscovery()
        NotificationCenter.default.removeObserver(self)
        hapticEngine?.stop(completionHandler: nil)
        hapticEngine = nil
    }

    deinit {
        teardown()
    }
}
