// Hybrid Nonogram Solver - Propagation + Glucose-Syrup SAT
// 
// Best of both worlds:
// 1. Fast constraint propagation first (often solves puzzle completely)
// 2. Glucose-Syrup parallel SAT for remaining unknowns (no external dependencies)
//
// This is the recommended solver for best performance.

#include "nonogram.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <thread>

// Glucose Parallel headers
#include "parallel/MultiSolvers.h"
#include "parallel/ParallelSolver.h"

using GlucoseMultiSolver = Glucose::MultiSolvers;
using Glucose::Lit;
using Glucose::mkLit;
using Glucose::vec;
using Glucose::lbool;

class HybridNonogramSolver {
public:
    nonogram::Puzzle puzzle;
    int width, height;
    std::vector<int8_t> grid;  // 0=white, 1=black, 2=undecided
    int next_var;
    std::vector<std::vector<int>> cell_var;
    bool has_col_symmetry = false;
    
    // Stats
    int unknowns_after_prop = 0;
    double prop_time_ms = 0;
    double sat_time_ms = 0;
    
    HybridNonogramSolver(const nonogram::Puzzle& p) : puzzle(p), width(p.width), height(p.height) {
        grid.resize(width * height, 2);
        cell_var.resize(height, std::vector<int>(width, 0));
        next_var = 1;
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c)
                cell_var[r][c] = next_var++;
        detect_symmetry();
    }
    
    void detect_symmetry() {
        has_col_symmetry = true;
        for (int c = 0; c < width / 2; ++c) {
            if (puzzle.col_descriptions[c] != puzzle.col_descriptions[width - 1 - c]) {
                has_col_symmetry = false;
                break;
            }
        }
    }
    
    // Phase 1: Run constraint propagation
    int run_propagation(double timeout_ms) {
        auto t1 = std::chrono::high_resolution_clock::now();
        
        nonogram::Solver solver(puzzle);
        solver.set_timeout(timeout_ms);
        auto result = solver.solve();
        
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c) {
                int8_t val = static_cast<int8_t>(result.get(r, c));
                grid[r * width + c] = val;
            }
        
        int unknowns = 0;
        for (int i = 0; i < width * height; ++i) 
            if (grid[i] == 2) unknowns++;
        
        auto t2 = std::chrono::high_resolution_clock::now();
        prop_time_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        unknowns_after_prop = unknowns;
        
        return unknowns;
    }
    
    // Phase 2: SAT solving with Glucose-Syrup for remaining unknowns
    bool run_sat() {
        auto t1 = std::chrono::high_resolution_clock::now();
        
        GlucoseMultiSolver solver;
        solver.setVerbosity(0);
        solver.use_simplification = true;
        
        // Create variables
        for (int i = 0; i < next_var; i++) {
            solver.newVar();
        }
        
        // Add unit clauses for already-determined cells
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                int var = cell_var[r][c];
                if (grid[r * width + c] == 1) {
                    vec<Lit> clause;
                    clause.push(mkLit(var, false));  // must be black
                    solver.addClause(clause);
                } else if (grid[r * width + c] == 0) {
                    vec<Lit> clause;
                    clause.push(mkLit(var, true));   // must be white
                    solver.addClause(clause);
                }
            }
        }
        
        // Add symmetry constraints if detected
        if (has_col_symmetry) {
            for (int r = 0; r < height; ++r) {
                for (int c = 0; c < width / 2; ++c) {
                    int v1 = cell_var[r][c];
                    int v2 = cell_var[r][width - 1 - c];
                    // v1 <-> v2
                    vec<Lit> c1, c2;
                    c1.push(mkLit(v1, true)); c1.push(mkLit(v2, false));
                    c2.push(mkLit(v1, false)); c2.push(mkLit(v2, true));
                    solver.addClause(c1);
                    solver.addClause(c2);
                }
            }
        }
        
        // Encode row constraints
        for (int r = 0; r < height; ++r) {
            std::vector<int> cells(width);
            for (int c = 0; c < width; ++c) cells[c] = cell_var[r][c];
            if (!encode_line(solver, puzzle.row_descriptions[r], cells, width)) {
                return false;
            }
        }
        
        // Encode column constraints
        for (int c = 0; c < width; ++c) {
            std::vector<int> cells(height);
            for (int r = 0; r < height; ++r) cells[r] = cell_var[r][c];
            if (!encode_line(solver, puzzle.col_descriptions[c], cells, height)) {
                return false;
            }
        }
        
        // Solve with parallel Glucose
        solver.adjustNumberOfCores();
        solver.generateAllSolvers();
        lbool result = solver.solve();
        
        auto t2 = std::chrono::high_resolution_clock::now();
        sat_time_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        
        if (result != l_True) return false;
        
        // Extract solution
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c)
                grid[r * width + c] = solver.model[cell_var[r][c]] == l_True ? 1 : 0;
        
        return true;
    }
    
    bool encode_line(GlucoseMultiSolver& solver, const nonogram::Description& desc,
                     const std::vector<int>& cells, int len) {
        int ns = desc.size();
        
        // Empty description: all white
        if (ns == 0) {
            for (int c : cells) {
                vec<Lit> clause;
                clause.push(mkLit(c, true));
                if (!solver.addClause(clause)) return false;
            }
            return true;
        }
        
        // Compute valid ranges for each segment
        std::vector<int> mn(ns), mx(ns);
        int p = 0;
        for (int i = 0; i < ns; ++i) { mn[i] = p; p += desc[i] + 1; }
        p = len;
        for (int i = ns - 1; i >= 0; --i) { p -= desc[i]; mx[i] = p; p--; }
        
        // Check feasibility
        for (int i = 0; i < ns; ++i) {
            if (mn[i] > mx[i]) return false;
        }
        
        // Create segment-start variables
        std::vector<std::vector<int>> sv(ns);
        for (int i = 0; i < ns; ++i) {
            sv[i].resize(len, 0);
            for (int q = mn[i]; q <= mx[i]; ++q) {
                sv[i][q] = next_var++;
                solver.newVar();
            }
        }
        
        // Exactly-one constraint for each segment
        for (int i = 0; i < ns; ++i) {
            // At least one
            vec<Lit> alo;
            for (int q = mn[i]; q <= mx[i]; ++q) {
                alo.push(mkLit(sv[i][q], false));
            }
            if (!solver.addClause(alo)) return false;
            
            // At most one (pairwise for small, sequential counter for large)
            std::vector<int> vars;
            for (int q = mn[i]; q <= mx[i]; ++q) vars.push_back(sv[i][q]);
            
            if (vars.size() <= 5) {
                for (size_t a = 0; a < vars.size(); ++a) {
                    for (size_t b = a + 1; b < vars.size(); ++b) {
                        vec<Lit> clause;
                        clause.push(mkLit(vars[a], true));
                        clause.push(mkLit(vars[b], true));
                        if (!solver.addClause(clause)) return false;
                    }
                }
            } else {
                // Sequential counter encoding
                int n = vars.size();
                std::vector<int> s(n - 1);
                for (int j = 0; j < n - 1; ++j) {
                    s[j] = next_var++;
                    solver.newVar();
                }
                vec<Lit> c;
                c.clear(); c.push(mkLit(vars[0], true)); c.push(mkLit(s[0], false));
                solver.addClause(c);
                for (int j = 1; j < n - 1; ++j) {
                    c.clear(); c.push(mkLit(s[j-1], true)); c.push(mkLit(s[j], false));
                    solver.addClause(c);
                    c.clear(); c.push(mkLit(vars[j], true)); c.push(mkLit(s[j], false));
                    solver.addClause(c);
                    c.clear(); c.push(mkLit(vars[j], true)); c.push(mkLit(s[j-1], true));
                    solver.addClause(c);
                }
                c.clear(); c.push(mkLit(vars[n-1], true)); c.push(mkLit(s[n-2], true));
                solver.addClause(c);
            }
        }
        
        // Ordering constraints between segments
        for (int i = 0; i < ns - 1; ++i) {
            for (int q = mn[i]; q <= mx[i]; ++q) {
                int min_next = q + desc[i] + 1;
                for (int q2 = mn[i+1]; q2 < min_next && q2 <= mx[i+1]; ++q2) {
                    vec<Lit> clause;
                    clause.push(mkLit(sv[i][q], true));
                    clause.push(mkLit(sv[i+1][q2], true));
                    if (!solver.addClause(clause)) return false;
                }
            }
        }
        
        // Link cell variables to segment positions
        for (int c = 0; c < len; ++c) {
            std::vector<int> covering;
            for (int i = 0; i < ns; ++i) {
                int pm = std::max(mn[i], c - desc[i] + 1);
                int px = std::min(mx[i], c);
                for (int q = pm; q <= px; ++q) {
                    if (sv[i][q]) covering.push_back(sv[i][q]);
                }
            }
            
            if (covering.empty()) {
                // No segment can cover this cell -> must be white
                vec<Lit> clause;
                clause.push(mkLit(cells[c], true));
                if (!solver.addClause(clause)) return false;
            } else {
                // cell BLACK <-> at least one covering segment placed
                // Forward: segment placed -> cell BLACK
                for (int v : covering) {
                    vec<Lit> clause;
                    clause.push(mkLit(v, true));
                    clause.push(mkLit(cells[c], false));
                    if (!solver.addClause(clause)) return false;
                }
                // Backward: cell BLACK -> some covering segment
                vec<Lit> clause;
                clause.push(mkLit(cells[c], true));
                for (int v : covering) {
                    clause.push(mkLit(v, false));
                }
                if (!solver.addClause(clause)) return false;
            }
        }
        
        return true;
    }
    
    void print_grid() const {
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width; ++c) {
                int8_t v = grid[r * width + c];
                std::cout << (v == 1 ? "█" : v == 0 ? "·" : "?");
            }
            std::cout << "\n";
        }
    }
};

int main(int argc, char* argv[]) {
    std::string filename = argc > 1 ? argv[1] : "webpbn_puzzles/1.non";
    // Default 500ms propagation - enough for easy puzzles, doesn't slow down hard ones
    double prop_timeout = argc > 2 ? std::stod(argv[2]) : 500;
    
    std::ifstream file(filename);
    if (!file) { 
        std::cerr << "Cannot open: " << filename << "\n"; 
        return 1; 
    }
    std::stringstream buf;
    buf << file.rdbuf();
    
    auto info = nonogram::parse_non_file_with_info(buf.str());
    if (!info) { 
        std::cerr << "Parse failed\n"; 
        return 1; 
    }
    
    int num_threads = std::thread::hardware_concurrency();
    std::cerr << "Puzzle: " << info->title << " (" << info->puzzle.width << "x" 
              << info->puzzle.height << ") [" << num_threads << " threads]\n";
    
    HybridNonogramSolver solver(info->puzzle);
    auto t_start = std::chrono::high_resolution_clock::now();
    
    // Phase 1: Propagation
    int unknowns = solver.run_propagation(prop_timeout);
    std::cerr << "After propagation: " << unknowns << " unknowns (" 
              << solver.prop_time_ms << "ms)\n";
    
    bool solved = false;
    if (unknowns == 0) {
        solved = true;
        std::cerr << "SOLVED by propagation alone!\n";
    } else {
        // Phase 2: SAT
        std::cerr << "Running Glucose-Syrup SAT solver...\n";
        solved = solver.run_sat();
        std::cerr << "SAT phase: " << solver.sat_time_ms << "ms\n";
    }
    
    auto t_end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    
    std::cerr << (solved ? "SOLVED" : "FAILED") << " in " << total << "ms\n";
    
    if (solved && info->puzzle.width <= 60) {
        solver.print_grid();
    }
    
    return solved ? 0 : 1;
}
