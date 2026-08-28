/**
 * MGB64-Bridging-Header.h — Objective-C/C bridging header for the Swift app.
 *
 * This file is referenced by the Xcode build setting
 * SWIFT_OBJC_BRIDGING_HEADER. It imports the C headers that Swift code
 * needs to call into the game engine via the GameBridge API.
 *
 * Only add headers here that define types or functions used from Swift.
 * The game engine's internal headers should NOT be exposed to Swift.
 */

#import "GameBridge.h"
