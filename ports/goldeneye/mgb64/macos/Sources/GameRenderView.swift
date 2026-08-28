/**
 * GameRenderView.swift — NSView subclass that provides the game's Metal rendering
 * surface and captures keyboard/mouse input for forwarding to the game engine.
 *
 * This view is the primary interface between AppKit's event system and the C
 * game engine (via GameBridge.h). It:
 *   1. Hosts a CAMetalLayer as its backing layer for the renderer to draw into.
 *   2. Translates keyboard events into N64 analog stick + button state.
 *   3. Translates mouse movement into look deltas and mouse buttons into fire/aim.
 *   4. Sends accumulated input to game_set_input() after every input event.
 *
 * Threading: All methods run on the main thread (AppKit event loop). The only
 * cross-thread call is game_set_input(), which is documented as THREAD-SAFE.
 */

import Cocoa
import QuartzCore

// MARK: - Key Code Constants

/// Raw key codes (hardware-independent virtual key codes from Carbon/Events.h).
/// These are the `event.keyCode` values for the US keyboard layout.
private enum KeyCode: UInt16 {
    case w          = 13
    case a          = 0
    case s          = 1
    case d          = 2
    case r          = 15
    case space      = 49
    case returnKey  = 36
    case tab        = 48
    case escape     = 53
    case shift      = 56   // Left Shift
    case rightShift = 60
    case one        = 18
    case two        = 19
    case three      = 20
    case four       = 21
}

// MARK: - GameRenderView

/// The primary rendering and input-capture view for the game.
///
/// Hosts a `CAMetalLayer` for the renderer and translates AppKit keyboard and
/// mouse events into `GameInputState` structs sent to the engine each event.
class GameRenderView: NSView {

    // MARK: Layer Setup

    override var wantsLayer: Bool {
        get { true }
        set { /* Ignored — always layer-backed. */ }
    }

    override func makeBackingLayer() -> CALayer {
        let layer = CAMetalLayer()
        layer.pixelFormat = .bgra8Unorm
        layer.framebufferOnly = true
        return layer
    }

    /// Typed accessor for the underlying Metal layer.
    var metalLayer: CAMetalLayer? {
        return self.layer as? CAMetalLayer
    }

    // MARK: Responder Chain

    override var acceptsFirstResponder: Bool { true }

    override func becomeFirstResponder() -> Bool { true }

    override func resignFirstResponder() -> Bool {
        unlockMouse()
        return super.resignFirstResponder()
    }

    // MARK: Window Integration

    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()

        guard let window = self.window, let metalLayer = self.metalLayer else {
            return
        }

        let scale = window.backingScaleFactor
        metalLayer.contentsScale = scale
        metalLayer.pixelFormat = .bgra8Unorm
    }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()

        // Remove stale tracking areas.
        for area in trackingAreas {
            removeTrackingArea(area)
        }

        // Track mouse movement across the entire view bounds.
        let options: NSTrackingArea.Options = [
            .mouseMoved,
            .activeInKeyWindow,
            .inVisibleRect
        ]
        let area = NSTrackingArea(rect: .zero, options: options, owner: self, userInfo: nil)
        addTrackingArea(area)
    }

    override func layout() {
        super.layout()

        guard let window = self.window else { return }

        let scale = window.backingScaleFactor
        let width = Int(bounds.width)
        let height = Int(bounds.height)

        game_notify_resize(Int32(width), Int32(height), Float(scale))
    }

    // MARK: - Input State

    /// Set of currently held-down key codes.
    private var pressedKeys: Set<UInt16> = []

    /// Accumulated mouse deltas since the last input send.
    private var mouseDeltaX: Float = 0
    private var mouseDeltaY: Float = 0

    /// Accumulated scroll wheel delta since the last input send.
    private var scrollWheelDelta: Int32 = 0

    /// Whether the left mouse button is currently held.
    private var isLeftMouseDown: Bool = false

    /// Whether the right mouse button is currently held.
    private var isRightMouseDown: Bool = false

    /// Whether the mouse is currently locked (captured) for gameplay.
    private(set) var isMouseLocked: Bool = false

    // MARK: - Keyboard Events

    override func keyDown(with event: NSEvent) {
        // Suppress the system beep for unhandled keys.
        guard !event.isARepeat else { return }
        pressedKeys.insert(event.keyCode)
        sendInputToGame()
    }

    override func keyUp(with event: NSEvent) {
        pressedKeys.remove(event.keyCode)
        sendInputToGame()
    }

    override func flagsChanged(with event: NSEvent) {
        let shiftPressed = event.modifierFlags.contains(.shift)

        if shiftPressed {
            pressedKeys.insert(KeyCode.shift.rawValue)
        } else {
            pressedKeys.remove(KeyCode.shift.rawValue)
            pressedKeys.remove(KeyCode.rightShift.rawValue)
        }

        sendInputToGame()
    }

    // MARK: - Mouse Events

    override func mouseMoved(with event: NSEvent) {
        accumulateMouseDelta(event)
        sendInputToGame()
    }

    override func mouseDragged(with event: NSEvent) {
        accumulateMouseDelta(event)
        sendInputToGame()
    }

    override func rightMouseDragged(with event: NSEvent) {
        accumulateMouseDelta(event)
        sendInputToGame()
    }

    override func mouseDown(with event: NSEvent) {
        // First click in the view locks the mouse for FPS gameplay
        if !isMouseLocked {
            lockMouse()
        }
        isLeftMouseDown = true
        sendInputToGame()
    }

    override func mouseUp(with event: NSEvent) {
        isLeftMouseDown = false
        sendInputToGame()
    }

    override func rightMouseDown(with event: NSEvent) {
        isRightMouseDown = true
        sendInputToGame()
    }

    override func rightMouseUp(with event: NSEvent) {
        isRightMouseDown = false
        sendInputToGame()
    }

    override func scrollWheel(with event: NSEvent) {
        // Positive scrollY = scroll up = weapon next.
        let delta = event.scrollingDeltaY
        if delta > 0 {
            scrollWheelDelta += 1
        } else if delta < 0 {
            scrollWheelDelta -= 1
        }
        sendInputToGame()
    }

    // MARK: - Mouse Lock / Unlock

    /// Lock the mouse cursor for FPS-style input capture.
    ///
    /// Hides the cursor and dissociates it from the mouse position so that
    /// the game receives raw deltas without the cursor hitting screen edges.
    func lockMouse() {
        guard !isMouseLocked else { return }
        isMouseLocked = true
        CGAssociateMouseAndMouseCursorPosition(0)
        NSCursor.hide()
    }

    /// Unlock the mouse cursor, restoring normal system behavior.
    func unlockMouse() {
        guard isMouseLocked else { return }
        isMouseLocked = false
        CGAssociateMouseAndMouseCursorPosition(1)
        NSCursor.unhide()
    }

    // MARK: - Input Assembly & Forwarding

    /// Accumulate raw mouse movement deltas from an event.
    private func accumulateMouseDelta(_ event: NSEvent) {
        mouseDeltaX += Float(event.deltaX)
        mouseDeltaY += Float(event.deltaY)
    }

    /// Build a `GameInputState` from the current pressed keys, mouse buttons,
    /// and accumulated deltas, then send it to the game engine.
    ///
    /// Called after every input event. The game engine's `game_set_input()`
    /// accumulates mouse deltas internally, so calling frequently is safe.
    private func sendInputToGame() {
        var state = GameInputState()

        // --- Analog stick from WASD ---
        let stickMagnitude: Int8 = 80

        var sx: Int8 = 0
        var sy: Int8 = 0

        if pressedKeys.contains(KeyCode.w.rawValue) { sy += stickMagnitude }
        if pressedKeys.contains(KeyCode.s.rawValue) { sy -= stickMagnitude }
        if pressedKeys.contains(KeyCode.d.rawValue) { sx += stickMagnitude }
        if pressedKeys.contains(KeyCode.a.rawValue) { sx -= stickMagnitude }

        state.stick_x = sx
        state.stick_y = sy

        // --- Buttons from keys ---
        var buttons: UInt16 = 0

        if pressedKeys.contains(KeyCode.space.rawValue) ||
           pressedKeys.contains(KeyCode.returnKey.rawValue) {
            buttons |= UInt16(GAME_BTN_A)
        }

        if pressedKeys.contains(KeyCode.shift.rawValue) ||
           pressedKeys.contains(KeyCode.rightShift.rawValue) {
            buttons |= UInt16(GAME_BTN_B)
        }

        if pressedKeys.contains(KeyCode.tab.rawValue) {
            buttons |= UInt16(GAME_BTN_START)
        }

        // Escape: unlock mouse if locked, otherwise send START (pause)
        if pressedKeys.contains(KeyCode.escape.rawValue) {
            if isMouseLocked {
                unlockMouse()
            } else {
                buttons |= UInt16(GAME_BTN_START)
            }
        }

        if pressedKeys.contains(KeyCode.r.rawValue) {
            buttons |= UInt16(GAME_BTN_R)
        }

        if pressedKeys.contains(KeyCode.one.rawValue) {
            buttons |= UInt16(GAME_BTN_CU)
        }
        if pressedKeys.contains(KeyCode.two.rawValue) {
            buttons |= UInt16(GAME_BTN_CD)
        }
        if pressedKeys.contains(KeyCode.three.rawValue) {
            buttons |= UInt16(GAME_BTN_CL)
        }
        if pressedKeys.contains(KeyCode.four.rawValue) {
            buttons |= UInt16(GAME_BTN_CR)
        }

        // --- Mouse buttons ---
        if isLeftMouseDown {
            buttons |= UInt16(GAME_BTN_Z)  // Fire (Z trigger on N64)
        }
        if isRightMouseDown {
            buttons |= UInt16(GAME_BTN_R)  // Aim / R shoulder
        }

        state.buttons = buttons

        // --- Mouse deltas ---
        state.mouse_dx = mouseDeltaX
        state.mouse_dy = mouseDeltaY

        // Reset accumulated deltas after sending.
        mouseDeltaX = 0
        mouseDeltaY = 0

        // --- Scroll wheel ---
        state.mouse_wheel = scrollWheelDelta
        scrollWheelDelta = 0

        // --- Right stick (unused for keyboard/mouse, zeroed) ---
        state.right_stick_x = 0
        state.right_stick_y = 0

        // --- Send to engine ---
        withUnsafePointer(to: &state) { ptr in
            game_set_input(ptr)
        }
    }
}
