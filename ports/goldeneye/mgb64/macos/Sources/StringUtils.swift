/**
 * StringUtils.swift — Shared string utilities for C interop.
 *
 * Extracted from ROMPickerView.swift to eliminate duplication.
 */

/// Convert a C fixed-size char array (imported as a tuple) to a Swift String.
func cStringFromTuple<T>(_ tuple: T) -> String {
    withUnsafePointer(to: tuple) {
        $0.withMemoryRebound(to: CChar.self, capacity: MemoryLayout<T>.size) {
            String(cString: $0)
        }
    }
}
