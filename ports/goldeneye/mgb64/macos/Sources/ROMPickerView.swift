/**
 * ROMPickerView.swift — First-launch ROM file selection sheet.
 *
 * Presents a drop zone + file browser for the user to provide a compatible
 * N64 ROM file (.z64, .v64, .n64). Validates the ROM via game_validate_rom()
 * from GameBridge and reports region, size, and byte order before continuing.
 */

import SwiftUI
import UniformTypeIdentifiers

// MARK: - UTType extensions for N64 ROM files

private extension UTType {
    static let z64ROM = UTType(filenameExtension: "z64") ?? .data
    static let v64ROM = UTType(filenameExtension: "v64") ?? .data
    static let n64ROM = UTType(filenameExtension: "n64") ?? .data
}

private let romFileExtensions: Set<String> = ["z64", "v64", "n64"]

// MARK: - ROMPickerView

@available(macOS 13.0, *)
struct ROMPickerView: View {
    /// Called with the validated ROM path when the user taps Continue.
    var onComplete: (String) -> Void

    @State private var selectedPath: String?
    @State private var validationResult: GameROMInfo?
    @State private var isValidating: Bool = false
    @State private var errorMessage: String?
    @State private var isDropTargeted: Bool = false
    @State private var showFileImporter: Bool = false

    var body: some View {
        VStack(spacing: 24) {
            header
            dropZone
            browseButton
            validationSection
            Spacer(minLength: 0)
            continueButton
        }
        .padding(32)
        .frame(width: 520, height: 480)
        .fileImporter(
            isPresented: $showFileImporter,
            allowedContentTypes: [.z64ROM, .v64ROM, .n64ROM, .data],
            allowsMultipleSelection: false
        ) { result in
            handleFileImporterResult(result)
        }
    }

    // MARK: - Subviews

    private var header: some View {
        VStack(spacing: 8) {
            Text("Select ROM File")
                .font(.title)
                .fontWeight(.semibold)

            Text("This app requires a compatible N64 ROM file to run.")
                .font(.subheadline)
                .foregroundStyle(.secondary)
        }
    }

    private var dropZone: some View {
        ZStack {
            RoundedRectangle(cornerRadius: 12)
                .strokeBorder(
                    isDropTargeted ? Color.accentColor : Color.secondary.opacity(0.4),
                    style: StrokeStyle(lineWidth: 2, dash: [8, 4])
                )
                .background(
                    RoundedRectangle(cornerRadius: 12)
                        .fill(isDropTargeted ? Color.accentColor.opacity(0.08) : Color.clear)
                )

            VStack(spacing: 12) {
                Image(systemName: "arrow.down.doc")
                    .font(.system(size: 36))
                    .foregroundStyle(.secondary)

                Text("Drop ROM file here")
                    .font(.headline)
                    .foregroundStyle(.secondary)

                Text(".z64 / .v64 / .n64")
                    .font(.caption)
                    .foregroundStyle(.tertiary)
            }
        }
        .frame(height: 140)
        .onDrop(of: [.fileURL], isTargeted: $isDropTargeted) { providers in
            handleDrop(providers)
        }
    }

    private var browseButton: some View {
        Button("Browse\u{2026}") {
            showFileImporter = true
        }
        .controlSize(.large)
        .disabled(isValidating)
    }

    @ViewBuilder
    private var validationSection: some View {
        if isValidating {
            HStack(spacing: 10) {
                ProgressView()
                    .controlSize(.small)
                Text("Validating ROM\u{2026}")
                    .foregroundStyle(.secondary)
            }
        } else if let info = validationResult {
            if info.valid {
                validROMInfoView(info)
            } else {
                invalidROMView(info)
            }
        } else if let error = errorMessage {
            Label {
                Text(error)
                    .foregroundStyle(.red)
            } icon: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.red)
            }
        }
    }

    private func validROMInfoView(_ info: GameROMInfo) -> some View {
        VStack(spacing: 8) {
            Label {
                Text(cStringFromTuple(info.message))
                    .fontWeight(.medium)
            } icon: {
                Image(systemName: "checkmark.circle.fill")
                    .foregroundStyle(.green)
            }

            HStack(spacing: 16) {
                InfoBadge(label: "Region", value: cStringFromTuple(info.region))
                InfoBadge(
                    label: "Size",
                    value: ByteCountFormatter.string(
                        fromByteCount: Int64(info.size_bytes),
                        countStyle: .file
                    )
                )
                InfoBadge(label: "Format", value: cStringFromTuple(info.byte_order))
            }
            .font(.caption)
        }
        .padding(12)
        .background(.green.opacity(0.06), in: RoundedRectangle(cornerRadius: 8))
    }

    private func invalidROMView(_ info: GameROMInfo) -> some View {
        VStack(spacing: 8) {
            Label {
                Text(cStringFromTuple(info.message))
                    .foregroundStyle(.red)
            } icon: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(.red)
            }

            Button("Try Again") {
                resetState()
            }
            .controlSize(.small)
        }
        .padding(12)
        .background(.red.opacity(0.06), in: RoundedRectangle(cornerRadius: 8))
    }

    private var continueButton: some View {
        Button("Continue") {
            if let path = selectedPath {
                onComplete(path)
            }
        }
        .controlSize(.large)
        .keyboardShortcut(.defaultAction)
        .disabled(!(validationResult?.valid ?? false))
    }

    // MARK: - Actions

    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first else { return false }

        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            guard
                let data = item as? Data,
                let url = URL(dataRepresentation: data, relativeTo: nil, isAbsolute: true)
            else { return }

            let ext = url.pathExtension.lowercased()
            guard romFileExtensions.contains(ext) else {
                DispatchQueue.main.async {
                    self.errorMessage = "Unsupported file type: .\(ext). Expected .z64, .v64, or .n64."
                }
                return
            }

            DispatchQueue.main.async {
                selectAndValidate(path: url.path)
            }
        }
        return true
    }

    private func handleFileImporterResult(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }

            let ext = url.pathExtension.lowercased()
            guard romFileExtensions.contains(ext) else {
                errorMessage = "Unsupported file type: .\(ext). Expected .z64, .v64, or .n64."
                return
            }

            // Start accessing security-scoped resource for sandboxed apps.
            let didAccess = url.startAccessingSecurityScopedResource()
            defer {
                if didAccess { url.stopAccessingSecurityScopedResource() }
            }
            selectAndValidate(path: url.path)

        case .failure(let error):
            // User cancelled — not an error worth surfacing.
            if (error as NSError).domain == NSCocoaErrorDomain,
               (error as NSError).code == NSUserCancelledError {
                return
            }
            errorMessage = error.localizedDescription
        }
    }

    private func selectAndValidate(path: String) {
        resetState()
        selectedPath = path
        isValidating = true

        // Run validation off the main thread to avoid blocking UI.
        DispatchQueue.global(qos: .userInitiated).async {
            let info = game_validate_rom(path)
            DispatchQueue.main.async {
                self.isValidating = false
                self.validationResult = info
                if !info.valid {
                    self.selectedPath = nil
                }
            }
        }
    }

    private func resetState() {
        selectedPath = nil
        validationResult = nil
        isValidating = false
        errorMessage = nil
    }
}

// MARK: - InfoBadge

/// Small label+value pill used to display ROM metadata.
private struct InfoBadge: View {
    let label: String
    let value: String

    var body: some View {
        VStack(spacing: 2) {
            Text(label)
                .font(.caption2)
                .foregroundStyle(.secondary)
            Text(value)
                .fontWeight(.medium)
        }
        .padding(.horizontal, 10)
        .padding(.vertical, 4)
        .background(.quaternary, in: RoundedRectangle(cornerRadius: 6))
    }
}
