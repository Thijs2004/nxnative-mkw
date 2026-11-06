// Launcher: the settings screen shown before the game starts (front_ios.m dm_show_launcher). Its settings come from the app's dm/launcher.json, which build_ipa.sh writes for this game
// from dm/games/settings.tsv (dm/games/settings.py): each sets an environment variable the host reads when the game starts. A variable an env file set (Documents/env.txt, the bundle's
// dm/env.txt; main.m lists them in DM_ENV_FILE_KEYS) is shown but kept. All settings share one screen, grouped by section (rows: LauncherRow.swift). Choices that differ from the game's default are kept
// in UserDefaults; Reset to tested defaults shows the defaults, and Play then forgets them. DM_LAUNCHER=0 in Documents/env.txt starts the game at once with the saved choices; DM_SAVED_CHOICES=0 ignores them (and keeps them)
import SwiftUI
import UIKit
import UniformTypeIdentifiers
import os

private func info(_ key: String) -> Any? { Bundle.main.object(forInfoDictionaryKey: key) }


/// Cloud saves: an iCloud Drive folder shared with the Mac app (chosen in its settings window's
/// Cloud saves too). The host brings the save in from there when the game starts and
/// sends it there after each save (dm/host/save_sync.h); the folder is a bookmark (CloudSaves.m).
private struct CloudSavesSection: View {
    @State private var folder: String = ""
    @State private var note: String = ""
    @State private var picking = false

    var body: some View {
        Section {
            HStack {
                Text("Cloud saves")
                Spacer()
                Text(folder.isEmpty ? "Off" : (folder as NSString).lastPathComponent)
                    .foregroundStyle(.secondary).lineLimit(1).truncationMode(.middle)
            }
            Button(folder.isEmpty ? "Choose a folder in iCloud Drive…" : "Choose a different folder…") { picking = true }
                // The sheet sits on the button, not on the Section: SwiftUI hands a Section's modifiers to every row, so one tap presented a picker per row and they invalidated each other (LNPANDY/NX2Apple#315).
                .sheet(isPresented: $picking) {
                    DocumentPicker(types: [.folder]) { url in
                        picking = false
                        if dm_store_cloud_saves(url) {
                            note = "Saved. The game uses this folder when you press Play."
                            refresh()
                        } else {
                            note = "That folder could not be remembered — try a different one."
                        }
                    } onCancel: { picking = false }
                }
            if !folder.isEmpty {
                Button("Turn off cloud saves", role: .destructive) {
                    dm_clear_cloud_saves(); refresh(); note = ""
                }
            }
        } footer: {
            Text(note.isEmpty
                 ? "Choose a folder in iCloud Drive, and the same one in the Mac app's Cloud saves. The save comes in when the game starts and goes out after each save; a save that is replaced is kept in save-backups."
                 : note)
        }
        .onAppear(perform: refresh)
    }

    private func refresh() {
        var stale: ObjCBool = false
        folder = dm_cloud_saves_path(&stale) ?? ""
        if stale.boolValue && folder.isEmpty {
            note = "The folder you chose before is no longer readable (the app was signed differently). Please choose it again."
        }
    }
}

/// UIDocumentPickerViewController for a folder, in the two callbacks a SwiftUI sheet wants.
private struct DocumentPicker: UIViewControllerRepresentable {
    var types: [UTType] = [.folder]
    let onPick: (URL) -> Void
    let onCancel: () -> Void

    func makeUIViewController(context: Context) -> UIDocumentPickerViewController {
        // .open (not .import): the folder stays where it is and the bookmark keeps pointing at it.
        let picker = UIDocumentPickerViewController(forOpeningContentTypes: types, asCopy: false)
        picker.allowsMultipleSelection = false
        picker.delegate = context.coordinator
        return picker
    }

    func updateUIViewController(_ vc: UIDocumentPickerViewController, context: Context) {}

    func makeCoordinator() -> Coordinator { Coordinator(onPick: onPick, onCancel: onCancel) }

    final class Coordinator: NSObject, UIDocumentPickerDelegate {
        let onPick: (URL) -> Void
        let onCancel: () -> Void
        init(onPick: @escaping (URL) -> Void, onCancel: @escaping () -> Void) {
            self.onPick = onPick; self.onCancel = onCancel
        }
        func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
            if let url = urls.first { onPick(url) } else { onCancel() }
        }
        func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) { onCancel() }
    }
}

// Check iPhone: the Graphics preset for this phone, from its model (memory size and GPU decide: an 8 GB phone pages the game's memory, so it keeps Medium or below).
// Tested: iPhone 16 Pro Max (Medium holds 30 fps). The rest follows the chips' GPU cores, memory and cooling; a model this list does not know is judged by its memory
struct DeviceAdvice { let name, preset, why: String }
func deviceAdvice() -> DeviceAdvice {
    var u = utsname(); uname(&u)
    let id = withUnsafeBytes(of: &u.machine) { String(decoding: $0.prefix(while: { $0 != 0 }), as: UTF8.self) }
    let gb = Int((Double(ProcessInfo.processInfo.physicalMemory) / 1_073_741_824).rounded())
    let known: [String: (String, String, String)] = [
        "iPhone16,1": ("iPhone 15 Pro", "low", "A17 Pro, 8 GB, small body that heats up fast"),
        "iPhone16,2": ("iPhone 15 Pro Max", "low", "A17 Pro, 8 GB"),
        "iPhone17,3": ("iPhone 16", "low", "A18 with a 5-core GPU, 8 GB"),
        "iPhone17,4": ("iPhone 16 Plus", "low", "A18 with a 5-core GPU, 8 GB"),
        "iPhone17,5": ("iPhone 16e", "low", "A18 with a 4-core GPU, 8 GB"),
        "iPhone17,1": ("iPhone 16 Pro", "medium", "A18 Pro, 8 GB"),
        "iPhone17,2": ("iPhone 16 Pro Max", "medium", "A18 Pro, 8 GB (tested: 30 fps)"),
        "iPhone18,3": ("iPhone 17", "medium", "A19, 8 GB"),
        "iPhone18,5": ("iPhone 17e", "low", "A19 with fewer GPU cores, 8 GB"),
        "iPhone18,4": ("iPhone Air", "high", "A19 Pro, 12 GB, thin body"),
        "iPhone18,1": ("iPhone 17 Pro", "high", "A19 Pro, 12 GB, vapor chamber"),
        "iPhone18,2": ("iPhone 17 Pro Max", "high", "A19 Pro, 12 GB, vapor chamber"),
    ]
    if let k = known[id] { return DeviceAdvice(name: k.0, preset: k.1, why: k.2) }
    let major = Int(id.dropFirst("iPhone".count).split(separator: ",").first ?? "") ?? 0
    if gb >= 11 && major >= 19 { return DeviceAdvice(name: "iPhone 18 or newer (\(id))", preset: "full", why: "A20 or newer, \(gb) GB") }
    if gb >= 11 { return DeviceAdvice(name: id, preset: "high", why: "\(gb) GB") }
    if gb >= 8 { return DeviceAdvice(name: id, preset: "medium", why: "\(gb) GB") }
    return DeviceAdvice(name: id, preset: "low", why: "\(gb) GB: the game may not fit")
}

private struct LauncherView: View {
    @ObservedObject var m: LauncherModel
    let play: () -> Void
    @State private var memory = os_proc_available_memory()
    @State private var advice = ""

    var body: some View {
        GeometryReader { geometry in
            HStack(spacing: 0) {
                VStack(spacing: 16) {
                    if let icon = appIcon() {
                        Image(uiImage: icon).resizable().frame(width: 96, height: 96)
                            .clipShape(RoundedRectangle(cornerRadius: 22))
                            .accessibilityHidden(true)
                    }
                    Text((info("CFBundleDisplayName") as? String) ?? "Game")
                        .font(.title.weight(.bold)).multilineTextAlignment(.center)
                    Text("Version \((info("CFBundleShortVersionString") as? String) ?? "")")
                        .font(.callout).foregroundStyle(.secondary)
                    playButton
                    if m.settings.contains(where: { $0.env == "DM_PRESET" }) {
                        Button("Check iPhone") { checkPhone() }.controlSize(.large)
                        if !advice.isEmpty { Text(advice).font(.footnote).foregroundStyle(.secondary).multilineTextAlignment(.center) }
                    }
                }
                .padding(16)
                .frame(width: geometry.size.width / 3, height: geometry.size.height)
                List {
                    ForEach(m.sections, id: \.self) { section in
                        let rows = m.rows(in: section)
                        if !rows.isEmpty {
                            Section(section) {
                                ForEach(rows) { SettingRow(s: $0, m: m) }
                            }
                        }
                    }
                    Section {
                        Button("Reset to tested defaults") { m.reset() }
                            .disabled(!m.changed)
                    } footer: {
                        Text("Brings back all settings this game was tested with.")
                    }
                    CloudSavesSection()
                    Section {
                        HStack {
                            Text("Memory available")
                            Spacer()
                            Text(ByteCountFormatter.string(fromByteCount: Int64(memory), countStyle: .memory)).monospacedDigit().foregroundStyle(.secondary)
                        }
                    }
                }
                .listStyle(.insetGrouped)
                .contentMargins(.horizontal, 12, for: .scrollContent)
                .frame(width: geometry.size.width * 2 / 3)
            }
        }
        .preferredColorScheme(.dark)
        .task {
            while !Task.isCancelled {
                memory = os_proc_available_memory()
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }

    private func checkPhone() {
        let a = deviceAdvice()
        let label = m.settings.first(where: { $0.env == "DM_PRESET" })?.options?.first(where: { $0[0] == a.preset })?[1] ?? a.preset
        var text = "\(a.name) (\(a.why)): \(label)"
        if m.fixed.contains("DM_PRESET") { text += " — the preset is fixed by env.txt, not changed" }
        else { m.values["DM_PRESET"] = a.preset; m.presetPicked = true; m.applyPreset(a.preset) }
        if ProcessInfo.processInfo.isLowPowerModeEnabled { text += ". Low Power Mode is on: expect lower fps" }
        advice = text
        fputs("launcher: Check iPhone: \(text)\n", stderr)
    }

    @ViewBuilder var playButton: some View {
        let button = Button(action: play) { Text("Play").font(.title3.weight(.semibold)).frame(maxWidth: 220) }.controlSize(.large)
        if #available(iOS 26.0, *) { button.buttonStyle(.glassProminent) } else { button.buttonStyle(.borderedProminent) }
    }

    private func appIcon() -> UIImage? {
        if let i = UIImage(named: "AppIcon") { return i }
        guard let icons = info("CFBundleIcons") as? [String: Any], let primary = icons["CFBundlePrimaryIcon"] as? [String: Any],
              let files = primary["CFBundleIconFiles"] as? [String], let last = files.last else { return nil }
        return UIImage(named: last)
    }
}

// Shows the launcher over the root view controller and calls done once the settings are in the environment; false (done is not called) when there is no launcher
@_cdecl("dm_show_launcher")
public func dmShowLauncher(_ viewController: UnsafeMutableRawPointer, _ done: @convention(c) () -> Void) -> Bool {
    guard let url = Bundle.main.resourceURL?.appendingPathComponent("dm/launcher.json"), let data = try? Data(contentsOf: url) else { return false }
    guard let file = try? JSONDecoder().decode(SettingsFile.self, from: data), !file.settings.isEmpty else { fputs("[RED] launcher: dm/launcher.json cannot be read; starting the game\n", stderr); return false }
    if ProcessInfo.processInfo.environment["DM_LAUNCHER"] == "0" { fputs("launcher: off (DM_LAUNCHER=0)\n", stderr); LauncherModel(file.settings).applySaved(); return false }
    let parent = Unmanaged<UIViewController>.fromOpaque(viewController).takeUnretainedValue()
    let model = LauncherModel(file.settings)
    var host: UIViewController?
    let shownAt = Date(), memShown = os_proc_available_memory()   // the start-stutter A/B: how long the screen stayed and what memory the app had then and at Play
    let view = LauncherView(m: model) {
        guard let h = host else { return }
        host = nil
        fputs(String(format: "launcher: Play after %.1f s on the screen; memory available %llu MB at show, %llu MB at Play; preset picked by hand: %@\n", Date().timeIntervalSince(shownAt),
                     UInt64(memShown >> 20), UInt64(os_proc_available_memory() >> 20), model.presetPicked ? "yes" : "no"), stderr)
        model.apply()
        h.dismiss(animated: false) { done() }
    }
    let h = UIHostingController(rootView: view)
    h.modalPresentationStyle = .fullScreen
    host = h
    parent.present(h, animated: false)
    fputs("launcher: \(file.settings.count) settings shown\n", stderr)
    return true
}
