# Nonogram Solver

Fast nonogram solver combining constraint propagation with parallel SAT solving. Solves 159 puzzles including adversarial cases like "Domino Logic" series.

![Viewer screenshot](screenshot.png)

## Quick Start

```bash
# Build
mkdir build && cd build
cmake .. && make -j

# Solve a puzzle (recommended: hybrid solver)
./nonogram_hybrid puzzle.non

# Batch benchmark
./nonogram_bench puzzles_dir/ results.json
```

## Solvers

### `nonogram_hybrid` (Recommended)

**Propagation + Glucose-Syrup parallel SAT solver**

- Self-contained: no external dependencies
- Best performance on multi-core systems
- 3-5x faster than external solvers on hard puzzles

```
| Puzzle | Size   | External SAT | Hybrid (Glucose) | Speedup |
|--------|--------|--------------|------------------|---------|
| 10088  | 52x63  | 10.9s        | 3.7s             | 2.95x   |
| 18297  | 36x42  | 23.2s        | 4.8s             | 4.87x   |
```

### `nonogram_solver` (Legacy)

Uses external SAT solvers (Kissat/CryptoMiniSat). Requires them installed.

## Algorithm

**Phase 1: Constraint Propagation**

- Line-by-line propagation from [Batenburg & Kosters 2009](https://liacs.leidenuniv.nl/~kosterswa/pbn/icga09.pdf)
- `Settle`: find cells that must be black/white in all valid line placements
- `FullSettle`: iterate rows/columns until fixpoint
- 2-SAT for global implications across lines
- Most puzzles solve here in <100ms

**Phase 2: SAT Solving** (for hard puzzles)

When propagation stalls, encode remaining unknowns as SAT:

```
Variables: p[r][c] = pixel is black, s[segment][pos] = segment starts at position
Constraints:
  - Each segment starts at exactly one valid position
  - Pixel black ↔ covered by some segment
  - Sequential counter AMO encoding (O(n) clauses)
  - Symmetry breaking for symmetric puzzles
```

Uses **Glucose-Syrup** parallel CDCL solver (bundled, competition-winning).

## Results

```
159/159 puzzles solved (100%)
Hardest: "November 19, 1863" (65x100) - ~2 min
Most: <100ms via propagation alone
```

## Dependencies

- C++17 compiler
- CMake 3.14+
- zlib (for Glucose)
- pthreads

No external SAT solver needed for `nonogram_hybrid`.

## Viewer

```bash
python3 -m http.server 8080
# open http://localhost:8080/viewer.html
```

## References

- Batenburg & Kosters. *A Discrete Tomography Approach to Japanese Puzzles*. ICGA 2009.
- [Glucose SAT Solver](https://github.com/audemard/glucose) - bundled CDCL solver
- [webpbn.com](https://webpbn.com) - puzzle database
- [nonogram-db](https://github.com/mikix/nonogram-db) - puzzle format spec
