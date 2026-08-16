#!/usr/bin/env python3
"""Validate the dependency-free GitHub Pages artifact."""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class PageParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.duplicate_ids: set[str] = set()
        self.references: list[tuple[str, str]] = []

    def handle_starttag(
        self, tag: str, attrs: list[tuple[str, str | None]]
    ) -> None:
        attributes = dict(attrs)
        element_id = attributes.get("id")
        if element_id:
            if element_id in self.ids:
                self.duplicate_ids.add(element_id)
            self.ids.add(element_id)
        for attribute in ("href", "src"):
            value = attributes.get(attribute)
            if value:
                self.references.append((attribute, value))


def parse_page(path: Path) -> PageParser:
    parser = PageParser()
    parser.feed(path.read_text(encoding="utf-8"))
    parser.close()
    return parser


def main() -> int:
    argument_parser = argparse.ArgumentParser()
    argument_parser.add_argument("site", nargs="?", default="site")
    args = argument_parser.parse_args()

    root = Path(args.site).resolve()
    required = [
        "index.html",
        "styles.css",
        "app.js",
        "favicon.svg",
        "robots.txt",
        "sitemap.xml",
        ".nojekyll",
    ]
    errors: list[str] = []
    for relative in required:
        if not (root / relative).is_file():
            errors.append(f"missing required site file: {relative}")

    html_files = sorted(root.rglob("*.html")) if root.is_dir() else []
    if not html_files:
        errors.append("site has no HTML files")

    parsed = {path: parse_page(path) for path in html_files}
    for page, parser in parsed.items():
        display = page.relative_to(root)
        for duplicate in sorted(parser.duplicate_ids):
            errors.append(f"{display}: duplicate id #{duplicate}")

        for attribute, reference in parser.references:
            parts = urlsplit(reference)
            if parts.scheme in {"https", "mailto"}:
                continue
            if parts.scheme or parts.netloc:
                errors.append(f"{display}: unsupported {attribute} URL {reference!r}")
                continue
            if parts.path.startswith("/"):
                errors.append(
                    f"{display}: root-relative {attribute} breaks project Pages: {reference!r}"
                )
                continue

            target_path = unquote(parts.path)
            target = page if not target_path else (page.parent / target_path).resolve()
            try:
                target.relative_to(root)
            except ValueError:
                errors.append(f"{display}: {attribute} escapes site root: {reference!r}")
                continue
            if not target.exists():
                errors.append(f"{display}: missing local {attribute} target {reference!r}")
                continue
            if parts.fragment and target.suffix == ".html":
                target_parser = parsed.get(target)
                if target_parser is None:
                    target_parser = parse_page(target)
                    parsed[target] = target_parser
                fragment = unquote(parts.fragment)
                if fragment not in target_parser.ids:
                    errors.append(
                        f"{display}: missing fragment #{fragment} in {target.relative_to(root)}"
                    )

    if errors:
        for error in errors:
            print(f"site check: {error}")
        return 1

    print(f"site check: ok ({len(html_files)} HTML page, {len(required)} required files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
