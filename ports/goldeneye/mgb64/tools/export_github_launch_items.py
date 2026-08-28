#!/usr/bin/env python3
"""Export or recreate launch-safe GitHub labels and open issues.

This tool is for the pre-public repository replacement path. It exports current
labels and open issue bodies, but intentionally does not export comments, closed
issues, closed PRs, Actions history, or pull-request refs.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import subprocess
import sys
import urllib.parse
from pathlib import Path
from typing import Any


PRIVATE_TEXT_RE = re.compile(
    "|".join(
        re.escape(part)
        for part in (
            "/U" + "sers/",
            "Desktop/" + "dev",
            "/home/" + "adam",
            "github.com/akratch/0" + "07",
            "private " + "dev repo",
            "contaminated " + "private",
            "Clau" + "de",
            "session " + "handoff",
            "agent " + "memory",
            "must never go " + "public",
        )
    ),
    re.IGNORECASE,
)
SECRET_TEXT_RE = re.compile(
    r"AKIA[0-9A-Z]{16}"
    r"|ghp_[0-9A-Za-z_]{36,}"
    r"|github_pat_[0-9A-Za-z_]+"
    r"|xox[baprs]-[0-9A-Za-z-]+"
    r"|sk-[A-Za-z0-9]{20,}"
    r"|BEGIN (?:RSA|OPENSSH|EC|DSA) PRIVATE KEY"
)
SDK_NOTICE_RE = re.compile(
    r"UNPUBLISHED\s+PROPRI" + "ETARY"
    r"|may not be disclo" + "sed"
    r"|without the prior written (?:permission|cons" + "ent)"
    r"|RESTRICTED\s+RIG" + "HTS"
    r"|subparagraph \(c\)\(1\)\(ii\) of the Rig" + "hts",
    re.IGNORECASE,
)
COMMIT_LIKE_RE = re.compile(r"(?<![0-9A-Fa-f])([0-9A-Fa-f]{7,12}|[0-9A-Fa-f]{40})(?![0-9A-Fa-f])")
LABEL_COLOR_RE = re.compile(r"^#?[0-9A-Fa-f]{6}$")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Export/apply launch-safe GitHub labels and open issues."
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    export = sub.add_parser("export", help="export labels and open issues")
    export.add_argument("--repo", required=True, help="source repository, OWNER/REPO")
    export.add_argument("--out-dir", required=True, help="directory for JSON/Markdown export")
    export.add_argument(
        "--exclude-number",
        type=int,
        action="append",
        default=[],
        help="open issue number to exclude from export; repeat as needed",
    )

    apply = sub.add_parser("apply", help="apply an export to a fresh repository")
    apply.add_argument("--repo", required=True, help="target repository, OWNER/REPO")
    apply.add_argument("--input-dir", required=True, help="directory containing export JSON")
    apply.add_argument(
        "--yes",
        action="store_true",
        help="actually create/update labels and issues; otherwise dry-run",
    )

    return parser.parse_args()


def run_json(args: list[str]) -> Any:
    result = subprocess.run(
        args,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return json.loads(result.stdout)


def run_lines(args: list[str]) -> list[str]:
    result = subprocess.run(
        args,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return [line for line in result.stdout.splitlines() if line]


def git_reachable_shas() -> set[str]:
    try:
        return set(run_lines(["git", "rev-list", "HEAD"]))
    except (OSError, subprocess.CalledProcessError):
        return set()


def plausible_commit_token(token: str) -> bool:
    if token.isdigit():
        return False
    lowered = token.lower()
    return not lowered.startswith(
        (
            "000",
            "800",
            "801",
            "802",
            "803",
            "804",
            "805",
            "806",
            "807",
            "808",
            "809",
            "80a",
            "80b",
            "80c",
            "80d",
            "80e",
            "80f",
            "ed8",
        )
    )


def token_is_reachable(token: str, reachable: set[str]) -> bool:
    lowered = token.lower()
    return any(sha.startswith(lowered) or lowered.startswith(sha) for sha in reachable)


def validate_text(label: str, text: str, reachable: set[str]) -> list[str]:
    problems: list[str] = []
    if PRIVATE_TEXT_RE.search(text):
        problems.append(f"{label}: private/local pre-public text")
    if SECRET_TEXT_RE.search(text):
        problems.append(f"{label}: token-shaped secret text")
    if SDK_NOTICE_RE.search(text):
        problems.append(f"{label}: proprietary SDK notice fragment")

    stale_tokens = sorted(
        {
            match.group(1)
            for match in COMMIT_LIKE_RE.finditer(text)
            if plausible_commit_token(match.group(1))
            and reachable
            and not token_is_reachable(match.group(1), reachable)
        }
    )
    if stale_tokens:
        problems.append(f"{label}: non-current commit-like reference(s): {', '.join(stale_tokens[:8])}")
    return problems


def validate_payload(payload: dict[str, Any], reachable: set[str]) -> list[str]:
    problems: list[str] = []
    labels = payload.get("labels", [])
    issues = payload.get("issues", [])
    if not isinstance(labels, list):
        return ["payload: labels must be a list"]
    if not isinstance(issues, list):
        return ["payload: issues must be a list"]

    seen_labels: set[str] = set()
    for index, label in enumerate(labels):
        if not isinstance(label, dict):
            problems.append(f"label:{index}: label entry must be an object")
            continue
        name = str(label.get("name", ""))
        color = str(label.get("color", ""))
        description = str(label.get("description", ""))
        if not name:
            problems.append(f"label:{index}: missing name")
        elif name in seen_labels:
            problems.append(f"label:{name}: duplicate label")
        else:
            seen_labels.add(name)
        if not LABEL_COLOR_RE.match(color):
            problems.append(f"label:{name or index}: invalid color")
        problems.extend(validate_text(f"label:{name}", name, reachable))
        problems.extend(validate_text(f"label:{name}:description", description, reachable))

    for index, issue in enumerate(issues):
        if not isinstance(issue, dict):
            problems.append(f"issue:{index}: issue entry must be an object")
            continue
        source_number = issue.get("source_number", index)
        title = str(issue.get("title", ""))
        body = str(issue.get("body", ""))
        issue_labels = issue.get("labels", [])
        if not title:
            problems.append(f"issue:{source_number}: missing title")
        if not isinstance(issue_labels, list):
            problems.append(f"issue:{source_number}: labels must be a list")
            issue_labels = []
        for label in issue_labels:
            label_name = str(label)
            if label_name not in seen_labels:
                problems.append(f"issue:{source_number}: unknown label reference: {label_name}")
            problems.extend(validate_text(f"issue:{source_number}:label:{label_name}", label_name, reachable))
        problems.extend(validate_text(f"issue:{source_number}:title", title, reachable))
        problems.extend(validate_text(f"issue:{source_number}:body", body, reachable))

    return problems


def print_validation_problems(problems: list[str], action: str) -> None:
    print(f"{action} refused because launch issue data is not clean:", file=sys.stderr)
    for problem in problems:
        print(f"  - {problem}", file=sys.stderr)


def fetch_labels(repo: str) -> list[dict[str, str]]:
    labels = run_json(["gh", "api", f"repos/{repo}/labels", "--paginate"])
    labels = [
        {
            "name": item["name"],
            "color": item.get("color", ""),
            "description": item.get("description") or "",
        }
        for item in labels
    ]
    return sorted(labels, key=lambda item: item["name"].casefold())


def fetch_open_issues(repo: str, excluded: set[int]) -> list[dict[str, Any]]:
    issues = run_json(
        [
            "gh",
            "api",
            f"repos/{repo}/issues?state=open&per_page=100",
            "--paginate",
        ]
    )
    exported: list[dict[str, Any]] = []
    for item in issues:
        if item.get("pull_request"):
            continue
        number = int(item["number"])
        if number in excluded:
            continue
        exported.append(
            {
                "source_number": number,
                "title": item["title"],
                "body": item.get("body") or "",
                "labels": [label["name"] for label in item.get("labels", [])],
            }
        )
    return sorted(exported, key=lambda item: item["source_number"])


def write_markdown(path: Path, payload: dict[str, Any]) -> None:
    lines = [
        "# GitHub Launch Issue Export",
        "",
        f"- Source repository: `{payload['source_repo']}`",
        f"- Exported at: `{payload['generated_at']}`",
        f"- Labels: {len(payload['labels'])}",
        f"- Open issues: {len(payload['issues'])}",
        "",
        "## Issues",
        "",
    ]
    for issue in payload["issues"]:
        label_text = ", ".join(issue["labels"]) if issue["labels"] else "none"
        lines.extend(
            [
                f"### #{issue['source_number']} {issue['title']}",
                "",
                f"Labels: `{label_text}`",
                "",
            ]
        )
    path.write_text("\n".join(lines), encoding="utf-8")


def export_items(args: argparse.Namespace) -> int:
    reachable = git_reachable_shas()
    labels = fetch_labels(args.repo)
    issues = fetch_open_issues(args.repo, set(args.exclude_number))
    payload = {
        "format": "mgb64-github-launch-items-v1",
        "generated_at": dt.datetime.now(dt.timezone.utc).isoformat(),
        "source_repo": args.repo,
        "excluded_numbers": sorted(args.exclude_number),
        "labels": labels,
        "issues": issues,
    }

    problems = validate_payload(payload, reachable)
    if problems:
        print_validation_problems(problems, "Export")
        return 1

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    json_path = out_dir / "github_launch_items.json"
    md_path = out_dir / "github_launch_items.md"
    json_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_markdown(md_path, payload)
    print(f"Wrote {json_path}")
    print(f"Wrote {md_path}")
    print(f"Exported {len(labels)} labels and {len(issues)} open issues")
    return 0


def load_export(input_dir: str) -> dict[str, Any]:
    path = Path(input_dir) / "github_launch_items.json"
    return json.loads(path.read_text(encoding="utf-8"))


def label_exists(repo: str, name: str) -> bool:
    quoted = urllib.parse.quote(name, safe="")
    result = subprocess.run(
        ["gh", "api", f"repos/{repo}/labels/{quoted}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    return result.returncode == 0


def apply_label(repo: str, label: dict[str, str], dry_run: bool) -> None:
    name = label["name"]
    color = label["color"].lstrip("#")
    description = label.get("description", "")
    if dry_run:
        action = "update" if label_exists(repo, name) else "create"
        print(f"DRY-RUN label {action}: {name}")
        return

    quoted = urllib.parse.quote(name, safe="")
    if label_exists(repo, name):
        subprocess.run(
            [
                "gh",
                "api",
                "-X",
                "PATCH",
                f"repos/{repo}/labels/{quoted}",
                "-f",
                f"new_name={name}",
                "-f",
                f"color={color}",
                "-f",
                f"description={description}",
            ],
            check=True,
        )
    else:
        subprocess.run(
            [
                "gh",
                "api",
                "-X",
                "POST",
                f"repos/{repo}/labels",
                "-f",
                f"name={name}",
                "-f",
                f"color={color}",
                "-f",
                f"description={description}",
            ],
            check=True,
        )


def apply_issue(repo: str, issue: dict[str, Any], dry_run: bool) -> None:
    labels = issue.get("labels", [])
    if dry_run:
        label_text = ", ".join(labels) if labels else "none"
        print(f"DRY-RUN issue create: {issue['title']} [{label_text}]")
        return

    cmd = [
        "gh",
        "issue",
        "create",
        "--repo",
        repo,
        "--title",
        issue["title"],
        "--body",
        issue.get("body", ""),
    ]
    if labels:
        cmd.extend(["--label", ",".join(labels)])
    subprocess.run(cmd, check=True)


def apply_items(args: argparse.Namespace) -> int:
    payload = load_export(args.input_dir)
    if payload.get("format") != "mgb64-github-launch-items-v1":
        print("Unrecognized export format.", file=sys.stderr)
        return 2
    problems = validate_payload(payload, git_reachable_shas())
    if problems:
        print_validation_problems(problems, "Apply")
        return 1

    dry_run = not args.yes
    if dry_run:
        print("Dry run; pass --yes to modify the target repository.")

    for label in payload.get("labels", []):
        apply_label(args.repo, label, dry_run)
    for issue in payload.get("issues", []):
        apply_issue(args.repo, issue, dry_run)

    print(f"{'Planned' if dry_run else 'Applied'} {len(payload.get('labels', []))} labels")
    print(f"{'Planned' if dry_run else 'Created'} {len(payload.get('issues', []))} issues")
    return 0


def main() -> int:
    args = parse_args()
    if args.cmd == "export":
        return export_items(args)
    if args.cmd == "apply":
        return apply_items(args)
    raise AssertionError(args.cmd)


if __name__ == "__main__":
    raise SystemExit(main())
