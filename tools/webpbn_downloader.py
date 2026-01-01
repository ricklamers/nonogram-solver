#!/usr/bin/env python3
"""
Download puzzles from webpbn.com and convert them to .non format.

Usage:
    python webpbn_downloader.py --count 100 --output ./webpbn_puzzles
    python webpbn_downloader.py --ids 1,6,23,529 --output ./webpbn_puzzles
    python webpbn_downloader.py --range 1-500 --output ./webpbn_puzzles
"""

import argparse
import os
import re
import time
import urllib.request
import urllib.error
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from typing import Optional, List, Tuple


@dataclass
class WebpbnPuzzle:
    id: int
    title: str
    author: str
    copyright: str
    width: int
    height: int
    rows: List[List[int]]
    columns: List[List[int]]
    notes: str = ""
    
    def to_non_format(self) -> str:
        """Convert puzzle to .non file format."""
        lines = []
        lines.append(f'catalogue "webpbn.com #{self.id}"')
        lines.append(f'title "{self.title}"')
        if self.author:
            lines.append(f'by "{self.author}"')
        if self.copyright:
            lines.append(f'copyright "{self.copyright}"')
        lines.append(f'width {self.width}')
        lines.append(f'height {self.height}')
        lines.append('')
        lines.append('rows')
        for row in self.rows:
            if not row:
                lines.append('0')
            else:
                lines.append(','.join(map(str, row)))
        lines.append('')
        lines.append('columns')
        for col in self.columns:
            if not col:
                lines.append('0')
            else:
                lines.append(','.join(map(str, col)))
        lines.append('')
        return '\n'.join(lines)


def fetch_xml(puzzle_id: int, max_retries: int = 3) -> Optional[str]:
    """Fetch puzzle XML from webpbn.com."""
    url = f"https://webpbn.com/XMLpuz.cgi?id={puzzle_id}"
    
    for attempt in range(max_retries):
        try:
            req = urllib.request.Request(
                url,
                headers={'User-Agent': 'NonogramSolver/1.0 (research project)'}
            )
            with urllib.request.urlopen(req, timeout=30) as response:
                return response.read().decode('utf-8')
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None  # Puzzle doesn't exist
            print(f"  HTTP error {e.code} for puzzle {puzzle_id}, attempt {attempt + 1}")
        except urllib.error.URLError as e:
            print(f"  URL error for puzzle {puzzle_id}: {e.reason}, attempt {attempt + 1}")
        except Exception as e:
            print(f"  Error fetching puzzle {puzzle_id}: {e}, attempt {attempt + 1}")
        
        if attempt < max_retries - 1:
            time.sleep(2 ** attempt)  # Exponential backoff
    
    return None


def parse_xml(xml_content: str, puzzle_id: int) -> Optional[WebpbnPuzzle]:
    """Parse webpbn XML format into a puzzle object."""
    try:
        root = ET.fromstring(xml_content)
        
        # Find the puzzle element
        puzzle_elem = root.find('.//puzzle')
        if puzzle_elem is None:
            return None
        
        # Check if it's a black-and-white puzzle (we only support those)
        colors = puzzle_elem.findall('.//color')
        color_names = [c.get('name', '').lower() for c in colors]
        
        # We only handle black-and-white puzzles
        non_bw_colors = [c for c in color_names if c not in ('white', 'black', 'background')]
        if non_bw_colors:
            print(f"  Puzzle {puzzle_id} is colored ({non_bw_colors}), skipping")
            return None
        
        # Extract metadata
        title = puzzle_elem.findtext('title', f'Puzzle #{puzzle_id}')
        author = puzzle_elem.findtext('author', '')
        copyright_text = puzzle_elem.findtext('copyright', '')
        notes = puzzle_elem.findtext('note', '')
        
        # Extract clues
        rows = []
        columns = []
        
        for clues_elem in puzzle_elem.findall('.//clues'):
            clue_type = clues_elem.get('type', '')
            lines = []
            
            for line_elem in clues_elem.findall('line'):
                counts = []
                for count_elem in line_elem.findall('count'):
                    text = count_elem.text
                    if text and text.strip():
                        try:
                            counts.append(int(text.strip()))
                        except ValueError:
                            pass
                lines.append(counts)
            
            if clue_type == 'rows':
                rows = lines
            elif clue_type == 'columns':
                columns = lines
        
        if not rows or not columns:
            print(f"  Puzzle {puzzle_id} has no valid clues")
            return None
        
        width = len(columns)
        height = len(rows)
        
        return WebpbnPuzzle(
            id=puzzle_id,
            title=title,
            author=author,
            copyright=copyright_text,
            width=width,
            height=height,
            rows=rows,
            columns=columns,
            notes=notes
        )
        
    except ET.ParseError as e:
        print(f"  XML parse error for puzzle {puzzle_id}: {e}")
        return None
    except Exception as e:
        print(f"  Error parsing puzzle {puzzle_id}: {e}")
        return None


def download_puzzle(puzzle_id: int) -> Optional[WebpbnPuzzle]:
    """Download and parse a single puzzle."""
    xml_content = fetch_xml(puzzle_id)
    if xml_content is None:
        return None
    
    # Check for error responses
    if 'Error' in xml_content[:100] or 'No such puzzle' in xml_content:
        return None
    
    return parse_xml(xml_content, puzzle_id)


def save_puzzle(puzzle: WebpbnPuzzle, output_dir: str) -> str:
    """Save puzzle to .non file, return the filepath."""
    os.makedirs(output_dir, exist_ok=True)
    filepath = os.path.join(output_dir, f"{puzzle.id}.non")
    
    with open(filepath, 'w') as f:
        f.write(puzzle.to_non_format())
    
    return filepath


def get_puzzle_ids_to_try(args) -> List[int]:
    """Generate list of puzzle IDs to try downloading."""
    if args.ids:
        return [int(x.strip()) for x in args.ids.split(',')]
    
    if args.range:
        match = re.match(r'(\d+)-(\d+)', args.range)
        if match:
            start, end = int(match.group(1)), int(match.group(2))
            return list(range(start, end + 1))
    
    # Default: try a mix of known good IDs and sequential scanning
    # Known benchmark puzzles from webpbn survey
    known_ids = [
        1, 6, 16, 21, 23, 27, 65, 436, 529, 803, 1611, 1694, 2040,
        2413, 2556, 2712, 3541, 4645, 5123, 6574, 6739, 6884, 8098,
        9892, 10088, 10810, 12548, 14224, 16327, 18297, 20739, 22336
    ]
    
    # Add sequential IDs to fill up to requested count
    sequential_ids = list(range(1, max(args.count * 3, 500)))  # Try more than needed
    
    # Combine, prioritizing known IDs
    all_ids = known_ids + [x for x in sequential_ids if x not in known_ids]
    
    return all_ids


def main():
    parser = argparse.ArgumentParser(description='Download webpbn.com puzzles')
    parser.add_argument('--count', type=int, default=100,
                       help='Number of puzzles to download (default: 100)')
    parser.add_argument('--ids', type=str,
                       help='Comma-separated list of puzzle IDs')
    parser.add_argument('--range', type=str,
                       help='Range of puzzle IDs (e.g., 1-500)')
    parser.add_argument('--output', type=str, default='./webpbn_puzzles',
                       help='Output directory (default: ./webpbn_puzzles)')
    parser.add_argument('--delay', type=float, default=0.5,
                       help='Delay between requests in seconds (default: 0.5)')
    parser.add_argument('--min-size', type=int, default=5,
                       help='Minimum puzzle dimension (default: 5)')
    parser.add_argument('--max-size', type=int, default=100,
                       help='Maximum puzzle dimension (default: 100)')
    
    args = parser.parse_args()
    
    puzzle_ids = get_puzzle_ids_to_try(args)
    downloaded = []
    skipped = 0
    errors = 0
    
    print(f"Attempting to download {args.count} puzzles from webpbn.com...")
    print(f"Output directory: {args.output}")
    print()
    
    for i, puzzle_id in enumerate(puzzle_ids):
        if len(downloaded) >= args.count:
            break
        
        print(f"[{len(downloaded) + 1}/{args.count}] Trying puzzle #{puzzle_id}...", end=' ')
        
        puzzle = download_puzzle(puzzle_id)
        
        if puzzle is None:
            print("not found or error")
            errors += 1
            time.sleep(args.delay / 2)  # Shorter delay for errors
            continue
        
        # Size filter
        if puzzle.width < args.min_size or puzzle.height < args.min_size:
            print(f"too small ({puzzle.width}x{puzzle.height})")
            skipped += 1
            continue
        
        if puzzle.width > args.max_size or puzzle.height > args.max_size:
            print(f"too large ({puzzle.width}x{puzzle.height})")
            skipped += 1
            continue
        
        # Save the puzzle
        filepath = save_puzzle(puzzle, args.output)
        downloaded.append(puzzle)
        
        unique_info = ""
        if "unique" in puzzle.notes.lower():
            unique_info = " [unique]"
        elif "multiple" in puzzle.notes.lower():
            unique_info = " [multiple solutions]"
        
        print(f"✓ {puzzle.title} ({puzzle.width}x{puzzle.height}){unique_info}")
        
        time.sleep(args.delay)  # Be nice to the server
    
    print()
    print("=" * 60)
    print(f"Downloaded: {len(downloaded)} puzzles")
    print(f"Skipped:    {skipped} (size filters)")
    print(f"Errors:     {errors} (not found or parse errors)")
    print(f"Output:     {args.output}")
    print()
    
    # Print summary by size
    if downloaded:
        size_buckets = {}
        for p in downloaded:
            bucket = f"{(p.width // 10) * 10}-{(p.width // 10) * 10 + 9}x{(p.height // 10) * 10}-{(p.height // 10) * 10 + 9}"
            size_buckets[bucket] = size_buckets.get(bucket, 0) + 1
        
        print("Size distribution:")
        for bucket, count in sorted(size_buckets.items()):
            print(f"  {bucket}: {count}")


if __name__ == '__main__':
    main()
