/**
 * BrandColors.swift — Shared Color(hex:) extension for brand-themed views.
 *
 * Extracted from AboutView.swift and SplashView.swift to eliminate duplication.
 */

import SwiftUI

extension Color {
    /// Initializes a SwiftUI Color from a hex string (e.g., "#4A6FA5").
    init(hex: String) {
        let cleaned = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        var rgb: UInt64 = 0
        Scanner(string: cleaned).scanHexInt64(&rgb)

        let red = Double((rgb >> 16) & 0xFF) / 255.0
        let green = Double((rgb >> 8) & 0xFF) / 255.0
        let blue = Double(rgb & 0xFF) / 255.0

        self.init(red: red, green: green, blue: blue)
    }
}
