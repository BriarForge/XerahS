#!/usr/bin/env python3
"""Generate the BXIP001 KovaForge 0.29.0 baseline census and parity ledgers."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


TOOL_VERSION = "1.1.0"
BASELINE_COMMIT = "5c7e36dea77ab131fe0f5e2101e5d578ccde0306"
IMAGE_EDITOR_COMMIT = "651b1d8de4bc1f874790870560314670cd038684"
VIDEO_EDITOR_COMMIT = "0482ee322a3086d5af135aa9b54a37803241cb0d"
BASELINE_VERSION = "0.29.0"


@dataclass(frozen=True)
class Evidence:
    path: str
    symbol: str


def git(reference: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(reference), *args],
        check=True,
        capture_output=True,
        text=True,
    )
    return result.stdout.strip()


def rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def hash_files(root: Path, paths: Iterable[Path]) -> str:
    digest = hashlib.sha256()
    for path in sorted(set(paths)):
        digest.update(rel(path, root).encode("utf-8"))
        digest.update(b"\0")
        digest.update(path.read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def verify_reference(reference: Path) -> None:
    if git(reference, "rev-parse", "HEAD") != BASELINE_COMMIT:
        raise SystemExit(f"Reference HEAD must be {BASELINE_COMMIT}")
    if git(reference / "ShareX.ImageEditor", "rev-parse", "HEAD") != IMAGE_EDITOR_COMMIT:
        raise SystemExit(f"ImageEditor HEAD must be {IMAGE_EDITOR_COMMIT}")
    if git(reference / "ShareX.VideoEditor", "rev-parse", "HEAD") != VIDEO_EDITOR_COMMIT:
        raise SystemExit(f"VideoEditor HEAD must be {VIDEO_EDITOR_COMMIT}")
    if git(reference, "status", "--porcelain"):
        raise SystemExit("Reference working tree must be clean")


def find_matching_brace(text: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def extract_public_enums(reference: Path) -> list[dict[str, Any]]:
    roots = [
        reference / "src" / "desktop",
        reference / "src" / "platform",
        reference / "ShareX.ImageEditor" / "src",
        reference / "ShareX.VideoEditor" / "backend",
    ]
    output: list[dict[str, Any]] = []
    declaration = re.compile(r"\bpublic\s+enum\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)")
    member = re.compile(r"^\s*(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?:=[^,\r\n]+)?\s*,?\s*(?://.*)?$")
    for root in roots:
        if not root.exists():
            continue
        for path in root.rglob("*.cs"):
            text = path.read_text(encoding="utf-8-sig", errors="replace")
            for match in declaration.finditer(text):
                opening = text.find("{", match.end())
                closing = find_matching_brace(text, opening)
                if opening < 0 or closing < 0:
                    continue
                members: list[str] = []
                for line in text[opening + 1 : closing].splitlines():
                    stripped = line.strip()
                    if not stripped or stripped.startswith(("[", "//", "/*", "*", "#")):
                        continue
                    parsed = member.match(line)
                    if parsed:
                        members.append(parsed.group("name"))
                output.append(
                    {
                        "name": match.group("name"),
                        "path": rel(path, reference),
                        "line": text.count("\n", 0, match.start()) + 1,
                        "members": members,
                    }
                )
    return sorted(output, key=lambda item: (item["name"], item["path"]))


def settings_files(reference: Path) -> list[Path]:
    roots = [
        reference / "src" / "desktop" / "core",
        reference / "src" / "platform",
        reference / "src" / "desktop" / "app" / "XerahS.Assistant" / "Configuration",
        reference / "src" / "desktop" / "app" / "XerahS.RegionCapture" / "ScreenRecording",
        reference / "ShareX.ImageEditor" / "src" / "ShareX.ImageEditor" / "Hosting",
    ]
    files: set[Path] = set()
    for root in roots:
        for pattern in ("*Settings*.cs", "*Config*.cs"):
            files.update(root.rglob(pattern))
    files.add(reference / "src" / "desktop" / "app" / "XerahS.RegionCapture" / "ScreenRecording" / "RecordingModels.cs")
    image_editor_hosting = reference / "ShareX.ImageEditor" / "src" / "ShareX.ImageEditor" / "Hosting"
    files.update(
        {
            image_editor_hosting / "ImageEditorOptions.cs",
            image_editor_hosting / "ImageEditorToolbarItemOptions.cs",
            image_editor_hosting / "BackgroundRemoverOptions.cs",
        }
    )
    return sorted(path for path in files if "/obj/" not in path.as_posix())


def extract_image_editor_inventory(reference: Path, enums: list[dict[str, Any]], settings: list[dict[str, Any]]) -> dict[str, Any]:
    root = reference / "ShareX.ImageEditor"
    source_root = root / "src" / "ShareX.ImageEditor"
    source_files = sorted(source_root.rglob("*.cs"))
    ui_files = sorted(source_root.rglob("*.axaml"))
    projects = sorted(root.rglob("*.csproj"))
    docs = sorted((root / "docs").rglob("*.md"))
    tests = sorted(path for path in root.rglob("*.cs") if "test" in path.as_posix().lower())

    class_pattern = re.compile(
        r"\bpublic\s+(?P<abstract>abstract\s+)?(?:sealed\s+)?(?:partial\s+)?class\s+"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?::\s*(?P<bases>[^\{\r\n]+))?"
    )
    annotations: list[dict[str, Any]] = []
    effects: list[dict[str, Any]] = []
    commands: list[dict[str, Any]] = []
    relay_command_pattern = re.compile(
        r"\[RelayCommand(?:\([^\]]*\))?\]\s*"
        r"(?:public|private|protected|internal)\s+(?:async\s+)?[A-Za-z_][A-Za-z0-9_<>?\[\]]*\s+"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\(",
        re.MULTILINE,
    )
    for path in source_files:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        relative = rel(path, reference)
        for match in class_pattern.finditer(text):
            name = match.group("name")
            bases = [item.strip() for item in (match.group("bases") or "").split(",")]
            record = {
                "name": name,
                "path": relative,
                "line": text.count("\n", 0, match.start()) + 1,
                "base_types": bases,
            }
            if name.endswith("Annotation") and not match.group("abstract") and "/Core/Annotations/" in f"/{relative}":
                annotations.append(record)
            is_concrete_effect = any(base in {"ImageEffectBase", "AdjustmentImageEffectBase"} for base in bases)
            if is_concrete_effect and not match.group("abstract") and "/Core/ImageEffects/" in f"/{relative}":
                effect_id = re.search(r'public\s+override\s+string\s+Id\s*=>\s*"([^"]+)"', text)
                effect_name = re.search(r'public\s+override\s+string\s+Name\s*=>\s*"([^"]+)"', text)
                category = re.search(r"public\s+override\s+ImageEffectCategory\s+Category\s*=>\s*ImageEffectCategory\.([A-Za-z0-9_]+)", text)
                execution = re.search(r"public\s+override\s+EffectExecutionMode\s+ExecutionMode\s*=>\s*EffectExecutionMode\.([A-Za-z0-9_]+)", text)
                parameters = [
                    {"kind": parameter.group("kind"), "id": parameter.group("id"), "label": parameter.group("label")}
                    for parameter in re.finditer(
                        r'EffectParameters\.(?P<kind>[A-Za-z_][A-Za-z0-9_]*)(?:<[^>]+>)?\s*\(\s*"(?P<id>[^"]+)"\s*,\s*"(?P<label>[^"]+)"',
                        text,
                        re.MULTILINE,
                    )
                ]
                record.update(
                    {
                        "id": effect_id.group(1) if effect_id else None,
                        "display_name": effect_name.group(1) if effect_name else name.removesuffix("ImageEffect"),
                        "category": category.group(1) if category else path.parent.name,
                        "execution_mode": execution.group(1) if execution else "Previewable",
                        "parameters": parameters,
                    }
                )
                effects.append(record)
        if "/Presentation/ViewModels/" not in f"/{relative}":
            continue
        owner = path.stem.split(".", 1)[0]
        for match in relay_command_pattern.finditer(text):
            commands.append(
                {
                    "owner": owner,
                    "method": match.group("name"),
                    "path": relative,
                    "line": text.count("\n", 0, match.start()) + 1,
                }
            )

    editor_tool = next((item for item in enums if item["name"] == "EditorTool" and item["path"].startswith("ShareX.ImageEditor/")), None)
    operation_path = source_root / "Presentation" / "Effects" / "EditorOperationCatalog.cs"
    operation_text = operation_path.read_text(encoding="utf-8-sig", errors="replace")
    operations = [
        {"id": match.group(1), "kind": match.group(2), "path": rel(operation_path, reference)}
        for match in re.finditer(r'\("([^"]+)",\s*EditorOperationKind\.([A-Za-z0-9_]+)\)', operation_text)
    ]
    annotation_base_path = source_root / "Core" / "Annotations" / "Base" / "Annotation.cs"
    annotation_base_text = annotation_base_path.read_text(encoding="utf-8-sig", errors="replace")
    persistence_discriminators = [
        {"type": match.group(1), "discriminator": match.group(2), "path": rel(annotation_base_path, reference)}
        for match in re.finditer(r'JsonDerivedType\(typeof\(([A-Za-z0-9_]+)\),\s*typeDiscriminator:\s*"([^"]+)"\)', annotation_base_text)
    ]
    option_settings = [item for item in settings if item["path"].startswith("ShareX.ImageEditor/")]
    assets = sorted(path for path in source_root.rglob("*") if path.is_file() and "Assets" in path.parts)
    return {
        "commit": IMAGE_EDITOR_COMMIT,
        "projects": [rel(path, reference) for path in projects],
        "source_files": [rel(path, reference) for path in source_files],
        "ui_surfaces": [rel(path, reference) for path in ui_files],
        "editor_tools": editor_tool["members"] if editor_tool else [],
        "annotation_types": sorted(annotations, key=lambda item: item["name"]),
        "persistence_discriminators": persistence_discriminators,
        "effect_types": sorted(effects, key=lambda item: (str(item["category"]), str(item["id"]), item["name"])),
        "editor_operations": sorted(operations, key=lambda item: item["id"]),
        "commands": sorted(commands, key=lambda item: (item["owner"], item["method"], item["path"])),
        "settings": option_settings,
        "assets": [rel(path, reference) for path in assets],
        "test_sources": [rel(path, reference) for path in tests],
        "documentation": [rel(path, reference) for path in docs],
        "counts": {
            "projects": len(projects),
            "source_files": len(source_files),
            "ui_surfaces": len(ui_files),
            "editor_tools": len(editor_tool["members"]) if editor_tool else 0,
            "annotation_types": len(annotations),
            "persistence_discriminators": len(persistence_discriminators),
            "effect_types": len(effects),
            "effect_parameters": sum(len(item["parameters"]) for item in effects),
            "editor_operations": len(operations),
            "commands": len(commands),
            "settings": len(option_settings),
            "assets": len(assets),
            "test_sources": len(tests),
            "documentation": len(docs),
        },
        "input_hash": hash_files(reference, [*source_files, *ui_files, *projects, *docs, *assets]),
    }


def extract_settings(reference: Path, paths: list[Path]) -> list[dict[str, Any]]:
    class_pattern = re.compile(r"\bpublic\s+(?:sealed\s+|partial\s+)?class\s+([A-Za-z_][A-Za-z0-9_]*)")
    member_pattern = re.compile(
        r"^\s*public\s+(?!static\b|const\b|class\b|enum\b|interface\b|event\b)"
        r"(?P<type>[A-Za-z_][A-Za-z0-9_<>,.\[\]? ]*?)\s+"
        r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*(?P<tail>\{|=|;)"
    )
    output: list[dict[str, Any]] = []
    for path in paths:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        classes = [(match.start(), match.group(1)) for match in class_pattern.finditer(text)]
        if not classes:
            continue
        offset = 0
        for line_number, line in enumerate(text.splitlines(keepends=True), start=1):
            parsed = member_pattern.match(line)
            if parsed:
                declaring = max((entry for entry in classes if entry[0] <= offset), default=None)
                if declaring:
                    output.append(
                        {
                            "declaring_type": declaring[1],
                            "member": parsed.group("name"),
                            "type": " ".join(parsed.group("type").split()),
                            "path": rel(path, reference),
                            "line": line_number,
                            "declaration": line.strip(),
                        }
                    )
            offset += len(line)
    unique = {(item["declaring_type"], item["member"], item["path"]): item for item in output}
    return sorted(unique.values(), key=lambda item: (item["declaring_type"], item["member"], item["path"]))


def enum_members(enums: list[dict[str, Any]], name: str) -> list[tuple[str, Evidence]]:
    matches = [item for item in enums if item["name"] == name]
    if not matches:
        raise SystemExit(f"Required enum not found: {name}")
    chosen = matches[0]
    return [(member, Evidence(chosen["path"], f"{name}.{member}")) for member in chosen["members"]]


def slug(value: str) -> str:
    value = re.sub(r"([a-z0-9])([A-Z])", r"\1-\2", value)
    return re.sub(r"[^A-Za-z0-9]+", "-", value).strip("-").upper()


def platform_map(value: str = "required") -> dict[str, str]:
    return {"windows": value, "macos": value, "linux": value}


def evidence_map() -> dict[str, list[Any]]:
    return {"windows": [], "macos": [], "linux": []}


def row(identifier: str, domain: str, outcome: str, evidence: Evidence, *, contract: str | None = None, classification: str = "preserve") -> dict[str, Any]:
    return {
        "id": identifier,
        "domain": domain,
        "user_outcome": outcome,
        "classification": classification,
        "source_evidence": [{"path": evidence.path, "symbol": evidence.symbol}],
        "baseline_commit": BASELINE_COMMIT,
        "contract": contract,
        "platforms": platform_map(),
        "evidence": evidence_map(),
        "compatibility": [],
        "status": "inventoried",
    }


def capability_rows() -> list[dict[str, Any]]:
    entries = [
        ("APP-LAUNCH-001", "application-shell", "Launch one native application instance and activate the existing instance on repeated launch.", "src/desktop/app/XerahS.App/Program.cs", "Program.Main", None),
        ("APP-TRAY-001", "application-shell", "Control capture, upload, tools, history, settings, and lifecycle from the system tray.", "src/desktop/app/XerahS.UI/TrayIconHelper.cs", "TrayIconHelper", None),
        ("APP-ONBOARDING-001", "application-shell", "Complete first-run save-location, hotkey, OCR, and upload setup.", "src/desktop/app/XerahS.UI/Onboarding/OnboardingWizardWindow.axaml", "OnboardingWizardWindow", None),
        ("APP-NOTIFICATIONS-001", "application-shell", "Receive native task completion, action, and error notifications.", "src/desktop/app/XerahS.UI/Views/ToastWindow.axaml", "ToastWindow", None),
        ("APP-THEME-LOCALIZATION-001", "application-shell", "Select theme and language while retaining native accessibility semantics.", "src/desktop/core/XerahS.Core/Models/ApplicationConfig.cs", "ApplicationConfig.Language", None),
        ("APP-UPDATES-001", "application-shell", "Check the selected update channel and present update results safely.", "src/desktop/core/XerahS.Core/Models/ApplicationConfig.cs", "ApplicationConfig.AutoCheckUpdate", None),
        ("CONFIG-LIFECYCLE-001", "configuration-security", "Load, validate, back up, reset, import, and migrate application configuration.", "src/desktop/core/XerahS.Core/Managers/SettingsManager.cs", "SettingsManager", None),
        ("SECRETS-STORAGE-001", "configuration-security", "Store uploader and service secrets using protected platform storage.", "src/desktop/core/XerahS.Common/Settings/DPAPIEncryptedStringValueProvider.cs", "DPAPIEncryptedStringValueProvider", None),
        ("CAPTURE-FULLSCREEN-001", "capture", "Capture the complete virtual desktop.", "src/desktop/core/XerahS.Core/Enums.cs", "CaptureType.Fullscreen", None),
        ("CAPTURE-MONITOR-001", "capture", "Capture a selected or active monitor.", "src/desktop/core/XerahS.Core/Enums.cs", "CaptureType.Monitor", None),
        ("CAPTURE-WINDOW-001", "capture", "Capture a selected or active window.", "src/desktop/core/XerahS.Core/Enums.cs", "CaptureType.Window", None),
        ("CAPTURE-REGION-001", "capture", "Select and capture an arbitrary screen region.", "src/desktop/core/XerahS.Core/Enums.cs", "WorkflowType.RectangleRegion", "product-contract/capabilities/REGION-CAPTURE-001"),
        ("CAPTURE-SCROLLING-001", "capture", "Capture scrollable content into a combined result.", "src/desktop/app/XerahS.UI/Views/ScrollingCaptureWindow.axaml", "ScrollingCaptureWindow", None),
        ("CAPTURE-AUTO-001", "capture", "Capture a region repeatedly at a configured interval.", "src/desktop/app/XerahS.UI/Views/AutoCaptureWindow.axaml", "AutoCaptureWindow", None),
        ("WORKFLOW-INDEPENDENT-001", "workflow-engine", "Create independent workflows with their own hotkeys, capture choices, tasks, and destinations.", "src/desktop/core/XerahS.Core/Hotkeys/HotkeySettings.cs", "WorkflowSettings", None),
        ("WORKFLOW-ACTIONS-001", "workflow-engine", "Execute selected post-capture actions in a deterministic recoverable pipeline.", "src/platform/XerahS.Platform.Abstractions/TaskEnums.cs", "AfterCaptureTasks", "product-contract/capabilities/POST-CAPTURE-ACTIONS-001"),
        ("FILENAME-GENERATION-001", "workflow-engine", "Generate valid deterministic output names from configured tokens.", "src/desktop/core/XerahS.Core/Models/TaskSettings.cs", "TaskSettingsGeneral.NameFormatPattern", "product-contract/capabilities/FILENAME-GENERATION-001"),
        ("RECORD-VIDEO-001", "recording", "Record a selected screen area or window to video with pause, stop, abort, and recovery.", "src/desktop/app/XerahS.RegionCapture/ScreenRecording/ScreenRecorderService.cs", "ScreenRecorderService", None),
        ("RECORD-GIF-001", "recording", "Record a selected screen area or window to animated GIF.", "src/desktop/core/XerahS.Core/Enums.cs", "WorkflowType.ScreenRecorderGIF", None),
        ("RECORD-AUDIO-001", "recording", "Select and record supported audio sources with screen video.", "src/desktop/core/XerahS.Core/Models/TaskSettingsOptions.cs", "FFmpegOptions.AudioSource", None),
        ("EDITOR-SESSION-001", "image-editor", "Open, annotate, undo or redo, export, and round-trip an editor session.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Core/Editor/EditorCore.cs", "EditorCore", "product-contract/capabilities/EDITOR-SESSION-001"),
        ("EDITOR-ANNOTATIONS-001", "image-editor", "Create, select, transform, style, order, and delete supported annotations.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Core/Annotations", "Annotation tools", "product-contract/capabilities/EDITOR-ANNOTATIONS-001"),
        ("EDITOR-EFFECTS-001", "image-editor", "Discover, preview, parameterize, apply, cancel, favorite, and repeat supported image effects.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Core/ImageEffects", "Image effects", "product-contract/capabilities/EDITOR-EFFECTS-001"),
        ("EDITOR-CANVAS-001", "image-editor", "Create, open, insert, crop, resize, zoom, pan, compare, flatten, and manage the editor canvas.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Presentation/Controllers/EditorInputController.cs", "EditorInputController", "product-contract/capabilities/EDITOR-CANVAS-001"),
        ("EDITOR-OUTPUT-ACTIONS-001", "image-editor", "Copy, save, save as, upload, print, pin, set wallpaper, continue, or cancel an edited image.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Presentation/ViewModels/MainViewModel.cs", "MainViewModel commands", "product-contract/capabilities/EDITOR-OUTPUT-ACTIONS-001"),
        ("EDITOR-SETTINGS-001", "image-editor", "Configure editor appearance, defaults, tool styling, background, toolbar, recent items, and favorite effects.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Hosting/ImageEditorOptions.cs", "ImageEditorOptions", "product-contract/capabilities/EDITOR-SETTINGS-001"),
        ("EDITOR-UTILITIES-001", "image-editor", "Use the shipped color picker, QR, hash, icon, comparison, background-removal, and video-conversion utilities.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Presentation/Views/StartScreenDialogView.axaml", "StartScreenDialogView", "product-contract/capabilities/EDITOR-UTILITIES-001"),
        ("VIDEO-EDITOR-001", "video-editor-media", "Open media in the native video editor and save edited output.", "ShareX.VideoEditor/backend/ShareX.VideoEditor.csproj", "ShareX.VideoEditor", None),
        ("MEDIA-UTILITIES-001", "video-editor-media", "Combine, split, thumbnail, analyze, convert, index, and inspect supported media.", "src/desktop/app/XerahS.UI/Views/ToolsView.axaml", "ToolsView", None),
        ("UPLOAD-PIPELINE-001", "upload-destinations", "Upload image, text, or file content and retain result URLs and diagnostics.", "src/desktop/core/XerahS.Core/Tasks/Processors/UploadJobProcessor.cs", "UploadJobProcessor", None),
        ("UPLOAD-CUSTOM-001", "upload-destinations", "Import, edit, validate, and execute custom HTTP uploader definitions.", "src/desktop/core/XerahS.Uploaders/CustomUploader/CustomUploaderProvider.cs", "CustomUploaderProvider", None),
        ("UPLOAD-FIRST-PARTY-001", "upload-destinations", "Configure and use every shipped first-party destination provider.", "src/desktop/plugins", "First-party uploader projects", None),
        ("PLUGIN-EXTENSIBILITY-001", "upload-destinations", "Discover, validate, install, diagnose, and remove destination extensions.", "src/desktop/core/XerahS.Uploaders/PluginSystem/PluginLoader.cs", "PluginLoader", None),
        ("HISTORY-TASKS-001", "history-indexing", "Persist, browse, filter, search, reopen, and clean up task history.", "src/desktop/core/XerahS.History/HistoryManager.cs", "HistoryManager", None),
        ("HISTORY-MEDIA-001", "history-indexing", "Browse indexed media with thumbnails and re-edit supported items.", "src/desktop/app/XerahS.UI/Views/HistoryView.axaml", "HistoryView", None),
        ("AUTOMATION-CLI-001", "automation-integration", "Run capture, recording, upload, configuration, recovery, and diagnostic workflows from a stable CLI.", "src/desktop/cli/XerahS.CLI/Program.cs", "Program.BuildRootCommand", None),
        ("AUTOMATION-MCP-001", "automation-integration", "Expose capture, annotation, upload, history, settings, and workflow operations through MCP.", "src/tools/XerahS.McpServer/Server/XerahSMcpServer.cs", "XerahSMcpServer", None),
        ("AUTOMATION-ASSISTANT-001", "automation-integration", "Invoke configured assistant providers from UI and headless entry points.", "src/desktop/app/XerahS.Assistant", "XerahS.Assistant", None),
        ("INTEGRATION-WATCH-FOLDER-001", "automation-integration", "Watch configured folders and process new files in foreground or daemon mode.", "src/desktop/tools/XerahS.WatchFolder.Daemon", "XerahS.WatchFolder.Daemon", None),
        ("INTEGRATION-SHELL-001", "automation-integration", "Use Send To, context-menu, startup, and supported file associations.", "src/desktop/app/XerahS.UI/ViewModels/SettingsViewModel.Integration.cs", "SettingsViewModel integration settings", None),
        ("DIAGNOSTICS-RECOVERY-001", "diagnostics-recovery", "Diagnose permissions and dependencies, retain logs, and recover safely from partial failures.", "src/desktop/cli/XerahS.CLI/Commands/DoctorCommand.cs", "DoctorCommand", None),
        ("DISTRIBUTION-NATIVE-001", "distribution-quality", "Install, update, run, and uninstall a native package without losing user data.", "build", "Packaging definitions", None),
        ("MOBILE-EXPERIMENTAL-001", "mobile-experimental", "Keep experimental mobile surfaces visible to the census without including them in BXIP001 desktop parity.", "src/mobile-experimental", "Experimental mobile projects", None),
    ]
    rows = [row(identifier, domain, outcome, Evidence(path, symbol), contract=contract) for identifier, domain, outcome, path, symbol, contract in entries]
    rows[-1]["classification"] = "out-of-scope-bxip001"
    rows[-1]["platforms"] = platform_map("not-applicable")
    rows[-1]["approval"] = "product-owner-recorded-by-D-BASE-001"
    return rows


def settings_rows(settings: list[dict[str, Any]]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for item in settings:
        result = row(
            f"SET-{slug(item['declaring_type'])}-{slug(item['member'])}-001",
            "settings",
            f"Preserve {item['declaring_type']}.{item['member']} semantics, default, validation, migration, and downstream effect.",
            Evidence(item["path"], f"{item['declaring_type']}.{item['member']}"),
            contract="product-contract/capabilities/EDITOR-SETTINGS-001" if item["path"].startswith("ShareX.ImageEditor/") else None,
        )
        result["value_type"] = item["type"]
        result["source_declaration"] = item["declaration"]
        rows.append(result)
    return rows


def workflow_rows(enums: list[dict[str, Any]], image_editor: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    groups = [
        ("WORKFLOW", "WorkflowType", "Invoke the {name} workflow outcome."),
        ("AFTER-CAPTURE", "AfterCaptureTasks", "Run the {name} post-capture action when selected."),
        ("AFTER-UPLOAD", "AfterUploadTasks", "Run the {name} post-upload action when selected."),
    ]
    for prefix, enum_name, template in groups:
        for name, evidence in enum_members(enums, enum_name):
            rows.append(row(f"WF-{prefix}-{slug(name)}-001", "workflow", template.format(name=name), evidence))
    tool_path = next(item["path"] for item in enums if item["name"] == "EditorTool" and item["path"].startswith("ShareX.ImageEditor/"))
    for name in image_editor["editor_tools"]:
        rows.append(row(f"WF-EDITOR-TOOL-{slug(name)}-001", "image-editor-tool", f"Select and operate the {name} editor tool.", Evidence(tool_path, f"EditorTool.{name}"), contract="product-contract/capabilities/EDITOR-ANNOTATIONS-001"))
    for effect in image_editor["effect_types"]:
        if not effect["id"]:
            continue
        rows.append(row(f"WF-EDITOR-EFFECT-{slug(effect['id'])}-001", "image-editor-effect", f"Preview or immediately apply the {effect['display_name']} effect with its declared parameters.", Evidence(effect["path"], effect["name"]), contract="product-contract/capabilities/EDITOR-EFFECTS-001"))
    for operation in image_editor["editor_operations"]:
        rows.append(row(f"WF-EDITOR-OPERATION-{slug(operation['id'])}-001", "image-editor-operation", f"Run the {operation['kind']} editor operation.", Evidence(operation["path"], operation["kind"]), contract="product-contract/capabilities/EDITOR-CANVAS-001"))
    for command in image_editor["commands"]:
        method = command["method"]
        if command["owner"] != "MainViewModel":
            contract = "product-contract/capabilities/EDITOR-UTILITIES-001"
        elif method in {"Copy", "Save", "SaveAs", "Upload", "Print", "PinToScreen", "Continue", "Cancel", "ExitEditor"}:
            contract = "product-contract/capabilities/EDITOR-OUTPUT-ACTIONS-001"
        elif method in {"SelectTool", "SetColor", "SetStrokeWidth", "DeleteSelected", "DuplicateSelected", "CopyAnnotation", "CutAnnotation", "Paste", "BringForward", "BringToFront", "SendBackward", "SendToBack", "ResetNumberCounter", "ClearAnnotations", "Undo", "Redo"}:
            contract = "product-contract/capabilities/EDITOR-ANNOTATIONS-001"
        elif method in {"OpenOptionsPanel", "OpenToolbarCustomizationDialog", "ToggleSettingsPanel", "ToggleToolbars"}:
            contract = "product-contract/capabilities/EDITOR-SETTINGS-001"
        elif method in {"InvertColors", "BlackAndWhite", "Sepia", "Polaroid", "EdgeDetect", "Emboss", "MeanRemoval", "Smooth", "ApplyGradientPreset", "ToggleEffectsPanel", "CloseEffectsPanel"}:
            contract = "product-contract/capabilities/EDITOR-EFFECTS-001"
        else:
            contract = "product-contract/capabilities/EDITOR-CANVAS-001"
        rows.append(row(f"WF-EDITOR-COMMAND-{slug(command['owner'])}-{slug(method)}-001", "image-editor-command", f"Invoke {command['owner']}.{method} through its available native editor surface.", Evidence(command["path"], f"{command['owner']}.{method}"), contract=contract))
    return rows


def interface_rows(reference: Path, image_editor: dict[str, Any]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    view_root = reference / "src" / "desktop" / "app" / "XerahS.UI"
    for path in sorted((view_root / "Views").rglob("*.axaml")) + sorted((view_root / "Onboarding").rglob("*.axaml")):
        name = path.stem
        rows.append(row(f"IF-GUI-{slug(name)}-001", "gui", f"Reach and operate the {name} user interface surface.", Evidence(rel(path, reference), name), classification="native-equivalent"))
    cli_root = reference / "src" / "desktop" / "cli" / "XerahS.CLI" / "Commands"
    command_pattern = re.compile(r"new\s+Command(?:<[^>]+>)?\s*\(\s*\"([^\"]+)\"")
    for path in sorted(cli_root.glob("*Command.cs")):
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        names = sorted(set(command_pattern.findall(text))) or [path.stem.removesuffix("Command").lower()]
        for name in names:
            rows.append(row(f"IF-CLI-{slug(path.stem.removesuffix('Command'))}-{slug(name)}-001", "cli", f"Invoke the '{name}' CLI command with stable output and exit behavior.", Evidence(rel(path, reference), name)))
    mcp_root = reference / "src" / "tools" / "XerahS.McpServer" / "Tools"
    for path in sorted(mcp_root.glob("*Tools.cs")):
        name = path.stem
        rows.append(row(f"IF-MCP-{slug(name)}-001", "mcp", f"Use the {name} MCP tool family through the supported transports.", Evidence(rel(path, reference), name)))
    for path in sorted((reference / "src" / "desktop" / "plugins").glob("*.Plugin/*.csproj")):
        name = path.parent.name.removesuffix(".Plugin")
        rows.append(row(f"IF-DESTINATION-{slug(name)}-001", "destination", f"Configure and invoke the shipped {name} destination provider.", Evidence(rel(path, reference), path.stem)))
    image_editor_root = "ShareX.ImageEditor/src/ShareX.ImageEditor/Presentation/"
    for relative in image_editor["ui_surfaces"]:
        if not relative.startswith(image_editor_root) or not any(segment in relative for segment in ("/Views/", "/Controls/")):
            continue
        surface = Path(relative).stem
        qualifier = slug(relative.removeprefix(image_editor_root).removesuffix(".axaml"))
        if any(name in surface for name in ("EffectBrowser", "EffectSlider", "SchemaDrivenEffect")):
            contract = "product-contract/capabilities/EDITOR-EFFECTS-001"
        elif surface in {"EditorOptionsPanel", "ToolbarCustomizationDialogView"}:
            contract = "product-contract/capabilities/EDITOR-SETTINGS-001"
        elif surface in {"EditorView", "EditorWindow", "EditorBuiltInToolbars", "NewImageDialogView", "InsertImageDialogView", "StartScreenDialogView"}:
            contract = "product-contract/capabilities/EDITOR-CANVAS-001"
        elif "/Controls/" in relative:
            contract = "product-contract/capabilities/EDITOR-ANNOTATIONS-001"
        else:
            contract = "product-contract/capabilities/EDITOR-UTILITIES-001"
        rows.append(row(f"IF-IMAGE-EDITOR-{qualifier}-001", "image-editor-gui", f"Reach and operate the {surface} native-equivalent image editor surface.", Evidence(relative, surface), contract=contract, classification="native-equivalent"))
    return rows


def compatibility_rows() -> list[dict[str, Any]]:
    entries = [
        ("COMPAT-APPLICATION-CONFIG-JSON-001", "configuration", "Read and migrate ApplicationConfig JSON without silent data loss.", "src/desktop/core/XerahS.Core/Managers/SettingsManager.cs", "SettingsManager.ApplicationConfigFilePath"),
        ("COMPAT-WORKFLOWS-CONFIG-JSON-001", "configuration", "Read and migrate independent workflow configuration JSON.", "src/desktop/core/XerahS.Core/Managers/SettingsManager.cs", "SettingsManager.HotkeysConfigFilePath"),
        ("COMPAT-UPLOADERS-CONFIG-JSON-001", "configuration", "Import supported legacy ShareX and XerahS uploader configuration.", "src/desktop/core/XerahS.Uploaders/LegacySupport/UploadersConfigImporter.cs", "UploadersConfigImporter"),
        ("COMPAT-SECRETS-STORE-001", "configuration", "Migrate protected secrets without exposing plaintext credentials.", "src/desktop/core/XerahS.Core/Managers/SettingsManager.cs", "SecretsStore"),
        ("COMPAT-HISTORY-SQLITE-001", "history", "Read, migrate, and preserve SQLite task history.", "src/desktop/core/XerahS.History/HistoryManagerSQLite.cs", "HistoryManagerSQLite"),
        ("COMPAT-HISTORY-JSON-001", "history", "Read supported JSON task history.", "src/desktop/core/XerahS.History/HistoryManagerJSON.cs", "HistoryManagerJSON"),
        ("COMPAT-HISTORY-XML-001", "history", "Read supported legacy XML task history.", "src/desktop/core/XerahS.History/HistoryManagerXML.cs", "HistoryManagerXML"),
        ("COMPAT-XANN-001", "annotation", "Round-trip versioned compressed .xann annotation sidecars and detect source mismatch.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Core/Persistence/XannProjectFileService.cs", "XannProjectFileService"),
        ("COMPAT-IMAGE-EDITOR-OPTIONS-001", "image-editor-settings", "Read, validate, migrate, and preserve supported ImageEditorOptions fields without silent loss.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Hosting/ImageEditorOptions.cs", "ImageEditorOptions"),
        ("COMPAT-IMAGE-EDITOR-HOST-001", "image-editor-host", "Preserve host-provided editor options, service callbacks, session outcomes, and ownership boundaries.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Hosting/AvaloniaIntegration.cs", "AvaloniaIntegration"),
        ("COMPAT-IMAGE-EDITOR-RASTER-001", "image-editor-raster", "Decode and encode supported raster formats with explicit format, quality, alpha, orientation, and color handling.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Presentation/Views/EditorImageFileWriter.cs", "EditorImageFileWriter"),
        ("COMPAT-IMAGE-EDITOR-CLIPBOARD-001", "image-editor-clipboard", "Exchange supported image and annotation payloads through native clipboards without leaking private data.", "ShareX.ImageEditor/src/ShareX.ImageEditor/Hosting/IClipboardService.cs", "IClipboardService"),
        ("COMPAT-SXCU-001", "uploader", "Import, edit, validate, and export ShareX custom uploader .sxcu definitions.", "src/desktop/core/XerahS.Uploaders/CustomUploader/CustomUploaderSettingsSerializer.cs", "CustomUploaderSettingsSerializer"),
        ("COMPAT-SXIE-001", "image-effects", "Import supported legacy .sxie image-effect presets.", "src/desktop/app/XerahS.UI", ".sxie importer"),
        ("COMPAT-XSDP-001", "plugin", "Validate and install signed or trusted .xsdp destination packages safely.", "src/desktop/core/XerahS.Uploaders/PluginSystem/PluginPackager.cs", "PluginPackager"),
        ("COMPAT-MCP-JSON-001", "automation", "Preserve MCP JSON request, response, resource, and error semantics.", "src/tools/XerahS.McpServer/JsonRpc/JsonRpcRequest.cs", "JsonRpcRequest"),
    ]
    rows = [row(identifier, domain, outcome, Evidence(path, symbol)) for identifier, domain, outcome, path, symbol in entries]
    editor_contracts = {
        "COMPAT-XANN-001": "product-contract/capabilities/EDITOR-SESSION-001",
        "COMPAT-IMAGE-EDITOR-OPTIONS-001": "product-contract/capabilities/EDITOR-SETTINGS-001",
        "COMPAT-IMAGE-EDITOR-HOST-001": "product-contract/capabilities/EDITOR-OUTPUT-ACTIONS-001",
        "COMPAT-IMAGE-EDITOR-RASTER-001": "product-contract/capabilities/EDITOR-OUTPUT-ACTIONS-001",
        "COMPAT-IMAGE-EDITOR-CLIPBOARD-001": "product-contract/capabilities/EDITOR-ANNOTATIONS-001",
    }
    for item in rows:
        if item["id"] in editor_contracts:
            item["contract"] = editor_contracts[item["id"]]
    return rows


def validate_ledgers(reference: Path, ledgers: dict[str, dict[str, Any]]) -> None:
    seen: set[str] = set()
    for name, ledger in ledgers.items():
        if name == "baseline-deltas.yaml":
            continue
        for item in ledger["items"]:
            identifier = item["id"]
            if identifier in seen:
                raise SystemExit(f"Duplicate parity ledger ID: {identifier}")
            seen.add(identifier)
            for evidence in item["source_evidence"]:
                if not (reference / evidence["path"]).exists():
                    raise SystemExit(f"Missing source evidence path for {identifier}: {evidence['path']}")


def collect(reference: Path) -> dict[str, Any]:
    projects = sorted(reference.glob("src/**/*.csproj")) + sorted((reference / "ShareX.ImageEditor").glob("src/**/*.csproj"))
    enums = extract_public_enums(reference)
    setting_paths = settings_files(reference)
    settings = extract_settings(reference, setting_paths)
    image_editor = extract_image_editor_inventory(reference, enums, settings)
    views = sorted((reference / "src" / "desktop").rglob("*.axaml"))
    tests = sorted((reference / "tests").rglob("*.cs")) + sorted((reference / "src" / "tools").rglob("*Tests.cs"))
    docs = sorted((reference / "docs").rglob("*.md"))
    plugins = sorted((reference / "src" / "desktop" / "plugins").glob("*.Plugin/*.csproj"))
    source_files = sorted((reference / "src").rglob("*.cs"))
    return {
        "schema_version": 1,
        "tool": {"name": "baseline-census", "version": TOOL_VERSION},
        "baseline": {"repository": "https://github.com/KovaForge/XerahS", "commit": BASELINE_COMMIT, "application_version": BASELINE_VERSION, "image_editor_commit": IMAGE_EDITOR_COMMIT, "video_editor_commit": VIDEO_EDITOR_COMMIT},
        "inventory": {"projects": [rel(path, reference) for path in projects], "public_enums": enums, "settings": settings, "ui_views": [rel(path, reference) for path in views], "first_party_plugins": [rel(path, reference) for path in plugins], "test_sources": [rel(path, reference) for path in tests], "image_editor": image_editor},
        "input_hashes": {"projects": hash_files(reference, projects), "registries_and_source": hash_files(reference, source_files), "settings": hash_files(reference, setting_paths), "ui": hash_files(reference, views), "tests": hash_files(reference, tests), "documentation": hash_files(reference, docs), "image_editor": image_editor["input_hash"], "video_editor": git(reference / "ShareX.VideoEditor", "rev-parse", "HEAD")},
        "counts": {"projects": len(projects), "public_enums": len(enums), "enum_members": sum(len(item["members"]) for item in enums), "settings": len(settings), "ui_views": len(views), "first_party_plugins": len(plugins), "test_sources": len(tests), "image_editor": image_editor["counts"]},
        "unresolved": [
            "Runtime observation remains required on Windows, macOS, and Linux.",
            "The checked-in graph report was built from 5bbdb7db rather than the pinned baseline and is supporting evidence only.",
            "Source-structure and user-journey reviewers must resolve false positives, missing dynamic surfaces, and dead code.",
            "ImageEditor has no dedicated test project at the pinned submodule commit; contract-derived conformance fixtures remain required.",
            "ImageEditor README integration/project claims and current solution/public surface require reconciliation.",
            "ImageEditor AVIF changelog claims conflict with the statically reachable PNG/JPEG/WebP Save As surface.",
            "Product-owner census-closure signature remains required.",
        ],
    }


def yaml_scalar(value: Any) -> str:
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, (int, float)):
        return str(value)
    return json.dumps(str(value), ensure_ascii=False)


def yaml_dump(value: Any, indent: int = 0) -> str:
    prefix = " " * indent
    if isinstance(value, dict):
        lines: list[str] = []
        for key, item in value.items():
            if isinstance(item, (dict, list)) and item:
                lines.append(f"{prefix}{key}:")
                lines.append(yaml_dump(item, indent + 2))
            elif item == []:
                lines.append(f"{prefix}{key}: []")
            elif item == {}:
                lines.append(f"{prefix}{key}: {{}}")
            else:
                lines.append(f"{prefix}{key}: {yaml_scalar(item)}")
        return "\n".join(lines)
    if isinstance(value, list):
        lines = []
        for item in value:
            if isinstance(item, dict):
                first_key = next(iter(item))
                lines.append(f"{prefix}- {first_key}: {yaml_scalar(item[first_key])}")
                remainder = {key: val for key, val in item.items() if key != first_key}
                if remainder:
                    lines.append(yaml_dump(remainder, indent + 2))
            else:
                lines.append(f"{prefix}- {yaml_scalar(item)}")
        return "\n".join(lines)
    return f"{prefix}{yaml_scalar(value)}"


def generated_files(reference: Path) -> dict[str, bytes]:
    census = collect(reference)
    baseline = {
        "schema_version": 1,
        "id": "kova-0.29.0",
        "repository": "https://github.com/KovaForge/XerahS",
        "branch_when_pinned": "develop",
        "commit": BASELINE_COMMIT,
        "application_version": BASELINE_VERSION,
        "observed_on": "2026-08-30",
        "working_tree_when_pinned": "clean-and-synchronized",
        "submodules": {"ShareX.ImageEditor": IMAGE_EDITOR_COMMIT, "ShareX.VideoEditor": VIDEO_EDITOR_COMMIT},
        "scope": {"included": ["windows-desktop", "macos-desktop", "linux-desktop", "shipped-companion-tools"], "excluded": [{"path": "src/mobile-experimental", "classification": "out-of-scope-bxip001", "authority": "D-BASE-001"}]},
        "inventory_tool": {"name": "baseline-census", "version": TOOL_VERSION},
        "input_hashes": census["input_hashes"],
        "census_status": "open",
        "closure_requirements": ["source-structure-review", "user-journey-review", "runtime-observation", "unresolved-symbol-review", "product-owner-signature"],
    }
    ledgers = {
        "capability-ledger.yaml": {"schema_version": 1, "baseline": "kova-0.29.0", "ledger": "capability", "items": capability_rows()},
        "settings-ledger.yaml": {"schema_version": 1, "baseline": "kova-0.29.0", "ledger": "settings", "items": settings_rows(census["inventory"]["settings"])},
        "workflow-ledger.yaml": {"schema_version": 1, "baseline": "kova-0.29.0", "ledger": "workflow", "items": workflow_rows(census["inventory"]["public_enums"], census["inventory"]["image_editor"])},
        "interface-ledger.yaml": {"schema_version": 1, "baseline": "kova-0.29.0", "ledger": "interface", "items": interface_rows(reference, census["inventory"]["image_editor"])},
        "compatibility-ledger.yaml": {"schema_version": 1, "baseline": "kova-0.29.0", "ledger": "compatibility", "items": compatibility_rows()},
        "baseline-deltas.yaml": {"schema_version": 1, "current_baseline": "kova-0.29.0", "transitions": []},
    }
    validate_ledgers(reference, ledgers)
    files = {
        "product-contract/reference-baselines/kova-0.29.0.yaml": (yaml_dump(baseline) + "\n").encode("utf-8"),
        "product-contract/reference-baselines/provenance/kova-0.29.0-census.json": (json.dumps(census, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
    }
    for name, value in ledgers.items():
        files[f"product-contract/parity/{name}"] = (yaml_dump(value) + "\n").encode("utf-8")
    return files


def write_or_check(repo: Path, files: dict[str, bytes], check: bool) -> None:
    failures: list[str] = []
    for relative, content in files.items():
        destination = repo / relative
        if check:
            existing = destination.read_bytes().replace(b"\r\n", b"\n") if destination.exists() else b""
            if existing != content:
                failures.append(relative)
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(content)
    if failures:
        raise SystemExit("Generated baseline artifacts are stale:\n  " + "\n  ".join(failures))


def validate_editor_contract_selections(repo: Path, census: dict[str, Any]) -> None:
    editor = census["inventory"]["image_editor"]
    annotations = json.loads((repo / "product-contract" / "capabilities" / "EDITOR-ANNOTATIONS-001" / "annotation-tools.json").read_text(encoding="utf-8-sig"))
    selected_tools = [item["id"] for item in annotations["tools"]]
    if selected_tools != editor["editor_tools"]:
        raise SystemExit("EDITOR-ANNOTATIONS-001 tool selection is stale or reordered")

    effect_selection = json.loads((repo / "product-contract" / "capabilities" / "EDITOR-EFFECTS-001" / "effect-catalog-selection.json").read_text(encoding="utf-8-sig"))
    effects = editor["effect_types"]
    actual_categories: dict[str, int] = {}
    actual_modes: dict[str, int] = {}
    for effect in effects:
        actual_categories[effect["category"]] = actual_categories.get(effect["category"], 0) + 1
        actual_modes[effect["execution_mode"]] = actual_modes.get(effect["execution_mode"], 0) + 1
    expected = effect_selection["expected"]
    if expected["total"] != len(effects) or expected["categories"] != actual_categories or expected["execution_modes"] != actual_modes:
        raise SystemExit("EDITOR-EFFECTS-001 catalog selection counts are stale")
    if expected["declared_parameter_controls"] != sum(len(effect["parameters"]) for effect in effects):
        raise SystemExit("EDITOR-EFFECTS-001 parameter-control count is stale")

    utilities = json.loads((repo / "product-contract" / "capabilities" / "EDITOR-UTILITIES-001" / "utility-catalog.json").read_text(encoding="utf-8-sig"))
    utility_by_id = {item["id"]: item for item in utilities["utilities"]}
    enum_by_name = {item["name"]: item["members"] for item in census["inventory"]["public_enums"] if item["path"].startswith("ShareX.ImageEditor/")}
    exact_enums = {
        ("qr-code", "scan_modes"): "QrCodeScanMode",
        ("hash-checker", "algorithms"): "HashCheckerAlgorithm",
        ("icon-converter", "bit_depths"): "IconBitDepth",
        ("background-remover", "devices"): "BackgroundRemovalDevice",
        ("video-converter", "codecs"): "VideoConverterCodec",
    }
    for (utility_id, field), enum_name in exact_enums.items():
        if set(utility_by_id[utility_id][field]) != set(enum_by_name[enum_name]):
            raise SystemExit(f"EDITOR-UTILITIES-001 {utility_id}.{field} is stale")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    reference = args.reference.resolve()
    repo = Path(__file__).resolve().parents[2]
    verify_reference(reference)
    files = generated_files(reference)
    census = json.loads(files["product-contract/reference-baselines/provenance/kova-0.29.0-census.json"])
    validate_editor_contract_selections(repo, census)
    write_or_check(repo, files, args.check)
    print(f"Baseline census {'verified' if args.check else 'generated'} for {BASELINE_COMMIT}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
