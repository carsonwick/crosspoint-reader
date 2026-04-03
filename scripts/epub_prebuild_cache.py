#!/usr/bin/env python3
"""
epub_prebuild_cache.py — Pre-build book.bin metadata cache for CrossPoint Reader.

Parses an EPUB's OPF/TOC and writes a binary book.bin in the exact format
expected by BookMetadataCache::load(). The file is embedded in the EPUB ZIP
at META-INF/crosspoint/book.bin.

When the device opens the EPUB and finds no cached book.bin on the SD card,
it extracts this embedded copy instead of running the full OPF+TOC parse
pipeline, eliminating the first-open indexing delay.

Requirements: Python 3.6+ (no third-party packages needed)

Usage:
  python scripts/epub_prebuild_cache.py book.epub
  python scripts/epub_prebuild_cache.py book.epub out.epub
"""

import argparse
import io
import os
import struct
import sys
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

# Must match BookMetadataCache.cpp:  constexpr uint8_t BOOK_CACHE_VERSION = 5;
BOOK_CACHE_VERSION = 5

# Path inside the EPUB ZIP where the pre-built cache is stored
EMBEDDED_BOOK_BIN = 'META-INF/crosspoint/book.bin'


# ---------------------------------------------------------------------------
# Binary serialisation helpers — must match lib/Serialization/Serialization.h
# All values are little-endian (ESP32-C3 is LE).
# writeString: uint32_t len + raw UTF-8 bytes (no null terminator)
# writePod<T>: raw sizeof(T) bytes
# ---------------------------------------------------------------------------

def write_string(buf: io.BytesIO, s: str) -> None:
    encoded = s.encode('utf-8')
    buf.write(struct.pack('<I', len(encoded)))
    buf.write(encoded)


def string_byte_size(s: str) -> int:
    """Size in bytes of a serialised string (4-byte length prefix + data)."""
    return 4 + len(s.encode('utf-8'))


# ---------------------------------------------------------------------------
# Path helpers — must match FsHelpers::normalisePath()
# ---------------------------------------------------------------------------

def normalise_path(path: str) -> str:
    """
    Resolve '..' components and strip leading slashes.
    Equivalent to FsHelpers::normalisePath() in lib/FsHelpers/FsHelpers.cpp.
    """
    parts = []
    for p in path.split('/'):
        if p == '..':
            if parts:
                parts.pop()
        elif p:
            parts.append(p)
    return '/'.join(parts)


def split_href_anchor(href: str) -> tuple:
    """
    Split 'path/file.html#anchor' → ('path/file.html', 'anchor').
    Matches the split done in TocNcxParser.cpp and TocNavParser.cpp.
    """
    pos = href.find('#')
    if pos != -1:
        return href[:pos], href[pos + 1:]
    return href, ''


# ---------------------------------------------------------------------------
# XML namespace helper
# ---------------------------------------------------------------------------

def strip_ns(tag: str) -> str:
    """Remove Clark-notation namespace from a tag: '{ns}name' → 'name'."""
    return tag.split('}')[-1] if '}' in tag else tag


# ---------------------------------------------------------------------------
# EPUB parsers
# ---------------------------------------------------------------------------

def parse_container(zf: zipfile.ZipFile) -> str:
    """
    Parse META-INF/container.xml.
    Returns the full-path of the OPF rootfile, or '' on failure.
    """
    try:
        data = zf.read('META-INF/container.xml').decode('utf-8', errors='replace')
    except KeyError:
        sys.exit('META-INF/container.xml not found in EPUB')

    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        sys.exit(f'container.xml parse error: {exc}')

    # Try with and without namespace
    for rf in root.iter():
        if strip_ns(rf.tag) == 'rootfile':
            path = rf.attrib.get('full-path', '')
            if path:
                return path
    return ''


def parse_opf(zf: zipfile.ZipFile, opf_path: str) -> dict:
    """
    Parse the OPF file at opf_path.

    Returns a dict with:
      title, author, language, cover_href, text_ref_href,
      base_path, spine (list of normalised hrefs), toc_ncx, toc_nav
    """
    try:
        data = zf.read(opf_path).decode('utf-8', errors='replace')
    except KeyError:
        sys.exit(f'OPF not found in EPUB: {opf_path}')

    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        sys.exit(f'OPF parse error: {exc}')

    base_path = opf_path[:opf_path.rfind('/') + 1]

    title = ''
    author = ''
    language = ''
    cover_item_id = ''      # from <meta name="cover" content="...">
    cover_href = ''
    guide_cover_page_href = ''
    text_ref_href = ''
    toc_ncx = ''
    toc_nav = ''
    manifest = {}           # id → normalised href

    # Collect spine idrefs in document order (spine element comes after manifest)
    spine_idrefs = []

    for elem in root.iter():
        tag = strip_ns(elem.tag)

        if tag == 'title' and not title:
            title = (elem.text or '').strip()

        elif tag == 'creator' and not author:
            author = (elem.text or '').strip()

        elif tag == 'language' and not language:
            language = (elem.text or '').strip()

        elif tag == 'meta':
            # EPUB 2 cover: <meta name="cover" content="item-id">
            if elem.attrib.get('name') == 'cover' and not cover_item_id:
                cover_item_id = elem.attrib.get('content', '')

        elif tag == 'item':
            item_id = elem.attrib.get('id', '')
            raw_href = elem.attrib.get('href', '')
            media_type = elem.attrib.get('media-type', '')
            props = elem.attrib.get('properties', '')

            href = normalise_path(base_path + raw_href)
            manifest[item_id] = href

            # EPUB 2 NCX
            if media_type == 'application/x-dtbncx+xml' and not toc_ncx:
                toc_ncx = href

            # EPUB 3 nav document
            if 'nav' in props.split() and not toc_nav:
                toc_nav = href

            # EPUB 3 cover image via properties
            if 'cover-image' in props.split() and not cover_href:
                cover_href = href

        elif tag == 'itemref':
            idref = elem.attrib.get('idref', '')
            if idref:
                spine_idrefs.append(idref)

        elif tag == 'reference':
            ref_type = elem.attrib.get('type', '')
            raw_href = elem.attrib.get('href', '')
            guide_href = normalise_path(base_path + raw_href)
            if ref_type == 'text' and not text_ref_href:
                text_ref_href = guide_href
            elif ref_type in ('cover', 'cover-page') and not guide_cover_page_href:
                guide_cover_page_href = guide_href

    # Resolve cover from EPUB 2 cover_item_id if not found via properties
    if not cover_href and cover_item_id:
        cover_href = manifest.get(cover_item_id, '')

    # Guide cover-page fallback: search for image inside the cover XHTML
    # Mirrors the logic in Epub::parseContentOpf()
    if not cover_href and guide_cover_page_href:
        try:
            page_data = zf.read(guide_cover_page_href).decode('utf-8', errors='replace')
            cover_page_base = guide_cover_page_href[:guide_cover_page_href.rfind('/') + 1]
            for pattern in ('xlink:href="', 'src="'):
                idx = page_data.find(pattern)
                while idx != -1:
                    idx += len(pattern)
                    end = page_data.find('"', idx)
                    if end != -1:
                        ref = page_data[idx:end]
                        ext = Path(ref).suffix.lower()
                        if ext in ('.jpg', '.jpeg', '.png', '.gif'):
                            cover_href = normalise_path(cover_page_base + ref)
                            break
                    idx = page_data.find(pattern, idx)
                if cover_href:
                    break
        except KeyError:
            pass

    # Resolve spine: idref → normalised href
    spine = []
    for idref in spine_idrefs:
        href = manifest.get(idref)
        if href:
            spine.append(href)

    return {
        'title': title,
        'author': author,
        'language': language,
        'cover_href': cover_href,
        'text_ref_href': text_ref_href,
        'base_path': base_path,
        'spine': spine,
        'toc_ncx': toc_ncx,
        'toc_nav': toc_nav,
    }


def parse_toc_ncx(zf: zipfile.ZipFile, ncx_path: str, base_path: str) -> list:
    """
    Parse a toc.ncx (EPUB 2).
    Returns list of (title, href, anchor, depth) matching TocNcxParser.cpp output.
    depth is 1-based (top-level navPoints = 1).
    """
    try:
        data = zf.read(ncx_path).decode('utf-8', errors='replace')
    except KeyError:
        return []

    try:
        root = ET.fromstring(data)
    except ET.ParseError:
        return []

    entries = []

    def recurse_nav_point(node, depth):
        """
        Matches TocNcxParser.cpp:
          - currentDepth incremented on <navPoint> open
          - entry created at </content> with currentDepth
          - nested navPoints processed after parent's entry
        """
        for child in node:
            if strip_ns(child.tag) != 'navPoint':
                continue
            label = ''
            src = ''
            # Collect label and content from direct children (not nested navPoints)
            for sub in child:
                stag = strip_ns(sub.tag)
                if stag == 'navLabel':
                    for t in sub:
                        if strip_ns(t.tag) == 'text':
                            label = (t.text or '').strip()
                elif stag == 'content':
                    src = sub.attrib.get('src', '')
            if label and src:
                full = normalise_path(base_path + src)
                href, anchor = split_href_anchor(full)
                entries.append((label, href, anchor, depth))
            # Recurse into nested navPoints at depth+1
            recurse_nav_point(child, depth + 1)

    nav_map = None
    for elem in root:
        if strip_ns(elem.tag) == 'navMap':
            nav_map = elem
            break
    if nav_map is not None:
        recurse_nav_point(nav_map, 1)

    return entries


def parse_toc_nav(zf: zipfile.ZipFile, nav_path: str) -> list:
    """
    Parse an EPUB 3 nav.xhtml.
    Returns list of (title, href, anchor, depth) matching TocNavParser.cpp output.
    depth = olDepth (1-based; each nested <ol> increases depth by 1).
    """
    try:
        data = zf.read(nav_path).decode('utf-8', errors='replace')
    except KeyError:
        return []

    # Base path of the nav file (hrefs in nav are relative to the nav file itself)
    nav_base = nav_path[:nav_path.rfind('/') + 1]

    try:
        root = ET.fromstring(data)
    except ET.ParseError:
        return []

    entries = []

    def find_toc_nav_elem(node):
        """Find <nav epub:type="toc"> anywhere in the tree."""
        if strip_ns(node.tag) == 'nav':
            for attr_name, attr_val in node.attrib.items():
                if strip_ns(attr_name) in ('type', 'epub:type') and attr_val == 'toc':
                    return node
        for child in node:
            result = find_toc_nav_elem(child)
            if result is not None:
                return result
        return None

    toc_nav_elem = find_toc_nav_elem(root)
    if toc_nav_elem is None:
        return entries

    def recurse_ol(ol_elem, depth):
        """
        Matches TocNavParser.cpp:
          - olDepth incremented on <ol>, decremented on </ol>
          - entry created at </a> with current olDepth
          - nested <ol> inside <li> increases depth for children
        """
        for li in ol_elem:
            if strip_ns(li.tag) != 'li':
                continue
            label = ''
            href_raw = ''
            nested_ols = []
            for child in li:
                ctag = strip_ns(child.tag)
                if ctag == 'a' and not label:
                    href_raw = child.attrib.get('href', '')
                    label = ''.join(child.itertext()).strip()
                elif ctag == 'ol':
                    nested_ols.append(child)
            if label and href_raw:
                full = normalise_path(nav_base + href_raw)
                href, anchor = split_href_anchor(full)
                entries.append((label, href, anchor, depth))
            for nested_ol in nested_ols:
                recurse_ol(nested_ol, depth + 1)

    for child in toc_nav_elem:
        if strip_ns(child.tag) == 'ol':
            recurse_ol(child, 1)
            break

    return entries


# ---------------------------------------------------------------------------
# book.bin builder
# ---------------------------------------------------------------------------

def build_book_bin(zf: zipfile.ZipFile, opf_info: dict, toc_entries: list) -> bytes:
    """
    Build book.bin in the exact binary format read by BookMetadataCache::load().

    Layout (all little-endian):
      uint8_t  BOOK_CACHE_VERSION
      uint32_t lutOffset              (= HEADER_A_SIZE + metadata_size)
      uint16_t spineCount
      uint16_t tocCount
      [metadata] title, author, language, coverItemHref, textReferenceHref  (each: uint32_t len + bytes)
      [LUT]      uint32_t[spineCount] + uint32_t[tocCount]                  (absolute offsets)
      [spine]    {writeString(href), uint32_t cumulativeSize, int16_t tocIndex} × spineCount
      [toc]      {writeString(title), writeString(href), writeString(anchor),
                  uint8_t level, int16_t spineIndex} × tocCount
    """
    spine = opf_info['spine']
    spine_count = len(spine)
    toc_count = len(toc_entries)

    # --- Index: spine href → spine index (for TOC → spine matching) ----------
    spine_href_to_idx = {href: i for i, href in enumerate(spine)}

    # --- toc_spine_indices[j] = spine index for toc entry j, or -1 ----------
    toc_spine_indices = []
    for (title, href, anchor, depth) in toc_entries:
        toc_spine_indices.append(spine_href_to_idx.get(href, -1))

    # --- spine_to_toc[i] = first toc entry index whose href matches spine[i] -
    # Propagate forward: spine items with no direct TOC entry inherit the last
    # known toc index (matches the device's behaviour in BookMetadataCache::buildBookBin).
    spine_to_toc = [-1] * spine_count
    for j, (title, href, anchor, depth) in enumerate(toc_entries):
        si = toc_spine_indices[j]
        if si != -1 and spine_to_toc[si] == -1:
            spine_to_toc[si] = j

    last_toc = -1
    for i in range(spine_count):
        if spine_to_toc[i] != -1:
            last_toc = spine_to_toc[i]
        else:
            spine_to_toc[i] = last_toc

    # --- Cumulative sizes (inflated size of each spine HTML file in the ZIP) --
    # size_t on ESP32-C3 = uint32_t (4 bytes); clamp to uint32 max
    cum_size = 0
    cum_sizes = []
    for href in spine:
        try:
            item_size = zf.getinfo(href).file_size
        except KeyError:
            item_size = 0
        cum_size += item_size
        cum_sizes.append(min(cum_size, 0xFFFFFFFF))

    # --- Serialise spine entries ---------------------------------------------
    spine_blobs = []
    for i, href in enumerate(spine):
        blob = io.BytesIO()
        write_string(blob, href)
        blob.write(struct.pack('<I', cum_sizes[i]))        # size_t = uint32_t
        blob.write(struct.pack('<h', spine_to_toc[i]))     # int16_t tocIndex
        spine_blobs.append(blob.getvalue())

    # --- Serialise TOC entries -----------------------------------------------
    toc_blobs = []
    for j, (title, href, anchor, depth) in enumerate(toc_entries):
        blob = io.BytesIO()
        write_string(blob, title)
        write_string(blob, href)
        write_string(blob, anchor)
        blob.write(struct.pack('<B', depth & 0xFF))         # uint8_t level
        blob.write(struct.pack('<h', toc_spine_indices[j])) # int16_t spineIndex
        toc_blobs.append(blob.getvalue())

    # --- Compute sizes and offsets -------------------------------------------
    HEADER_A_SIZE = 1 + 4 + 2 + 2   # version + lutOffset + spineCount + tocCount

    meta_buf = io.BytesIO()
    write_string(meta_buf, opf_info['title'])
    write_string(meta_buf, opf_info['author'])
    write_string(meta_buf, opf_info['language'])
    write_string(meta_buf, opf_info['cover_href'])
    write_string(meta_buf, opf_info['text_ref_href'])
    meta_bytes = meta_buf.getvalue()

    lut_offset = HEADER_A_SIZE + len(meta_bytes)
    lut_size = 4 * (spine_count + toc_count)

    # Absolute offsets of spine entries
    pos = lut_offset + lut_size
    spine_offsets = []
    for blob in spine_blobs:
        spine_offsets.append(pos)
        pos += len(blob)
    spine_data_size = pos - (lut_offset + lut_size)

    # Absolute offsets of TOC entries
    toc_offsets = []
    for blob in toc_blobs:
        toc_offsets.append(pos)
        pos += len(blob)

    # --- Assemble book.bin ---------------------------------------------------
    out = io.BytesIO()

    # Header A
    out.write(struct.pack('<B', BOOK_CACHE_VERSION))
    out.write(struct.pack('<I', lut_offset))
    out.write(struct.pack('<H', spine_count))
    out.write(struct.pack('<H', toc_count))

    # Metadata
    out.write(meta_bytes)

    # LUT: spine offsets then TOC offsets
    for off in spine_offsets:
        out.write(struct.pack('<I', off))
    for off in toc_offsets:
        out.write(struct.pack('<I', off))

    # Spine entries
    for blob in spine_blobs:
        out.write(blob)

    # TOC entries
    for blob in toc_blobs:
        out.write(blob)

    return out.getvalue()


# ---------------------------------------------------------------------------
# Main entry point
# ---------------------------------------------------------------------------

def process(epub_path: str, out_path: str) -> None:
    with zipfile.ZipFile(epub_path, 'r') as zf:
        # --- Parse -----------------------------------------------------------
        opf_path = parse_container(zf)
        if not opf_path:
            sys.exit('Could not find rootfile in container.xml')

        opf_info = parse_opf(zf, opf_path)

        print(f'Title:  {opf_info["title"] or "(unknown)"}')
        print(f'Author: {opf_info["author"] or "(unknown)"}')
        print(f'Spine:  {len(opf_info["spine"])} items')

        # Prefer EPUB 3 nav, fall back to EPUB 2 NCX
        toc_entries = []
        if opf_info['toc_nav']:
            toc_entries = parse_toc_nav(zf, opf_info['toc_nav'])
            print(f'TOC:    {len(toc_entries)} entries (nav)')
        if not toc_entries and opf_info['toc_ncx']:
            toc_entries = parse_toc_ncx(zf, opf_info['toc_ncx'], opf_info['base_path'])
            print(f'TOC:    {len(toc_entries)} entries (NCX)')
        if not toc_entries:
            print('TOC:    0 entries (none found)')

        # --- Build book.bin --------------------------------------------------
        book_bin = build_book_bin(zf, opf_info, toc_entries)
        print(f'book.bin: {len(book_bin)} bytes → {EMBEDDED_BOOK_BIN}')

        # --- Write output EPUB -----------------------------------------------
        with zipfile.ZipFile(out_path, 'w') as zout:
            # mimetype must be first entry, uncompressed (EPUB OCF spec)
            if 'mimetype' in zf.namelist():
                zout.writestr(zipfile.ZipInfo('mimetype'), zf.read('mimetype'))

            # Copy every entry except the one we are replacing
            for name in zf.namelist():
                if name == 'mimetype':
                    continue
                if name == EMBEDDED_BOOK_BIN:
                    continue  # replaced below
                info = zf.getinfo(name)
                zout.writestr(name, zf.read(name), compress_type=info.compress_type)

            # Embed pre-built book.bin (uncompressed — device reads sequentially)
            zout.writestr(EMBEDDED_BOOK_BIN, book_bin, compress_type=zipfile.ZIP_STORED)

    in_kb = os.path.getsize(epub_path) / 1024
    out_kb = os.path.getsize(out_path) / 1024
    print(f'Done.  {in_kb:.0f} KB → {out_kb:.0f} KB  (+{out_kb - in_kb:.0f} KB for cache)')


def main():
    parser = argparse.ArgumentParser(
        description='Pre-build CrossPoint book.bin cache and embed it in an EPUB'
    )
    parser.add_argument('epub', help='Input .epub file')
    parser.add_argument('out', nargs='?',
                        help='Output .epub file (default: in-place modification)')
    args = parser.parse_args()

    if not os.path.exists(args.epub):
        sys.exit(f'File not found: {args.epub}')

    out = args.out or args.epub
    if out == args.epub:
        tmp = args.epub + '.prebuilt.tmp'
        process(args.epub, tmp)
        os.replace(tmp, args.epub)
    else:
        process(args.epub, out)


if __name__ == '__main__':
    main()
