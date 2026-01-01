// Fast Nonogram Solver - Final Optimized Implementation
// 
// Optimizations:
// 1. Sequential counter AMO encoding (O(n) vs O(n²) clauses)
// 2. Automatic symmetry detection and exploitation
// 3. Adaptive SAT solver selection:
//    - Kissat for small problems (<100K clauses) - fastest single-threaded
//    - CryptoMiniSat for large problems - parallelism wins
// 4. Short propagation phase (2s) - hard puzzles don't benefit from more

#include "nonogram.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace nonogram;

class FastNonogramSolver {
public:
    Puzzle puzzle;
    int width, height;
    std::vector<int8_t> grid;
    int next_var;
    std::vector<std::vector<int>> clauses;
    std::vector<std::vector<int>> cell_var;
    bool has_col_symmetry = false;
    
    FastNonogramSolver(const Puzzle& p) : puzzle(p), width(p.width), height(p.height) {
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
    
    void add_clause(const std::vector<int>& c) { clauses.push_back(c); }
    
    // Sequential counter: O(n) clauses instead of O(n²)
    void at_most_one(const std::vector<int>& lits) {
        int n = lits.size();
        if (n <= 1) return;
        if (n <= 5) {  // Pairwise for tiny sets
            for (int i = 0; i < n; ++i)
                for (int j = i+1; j < n; ++j)
                    add_clause({-lits[i], -lits[j]});
            return;
        }
        std::vector<int> s(n-1);
        for (int i = 0; i < n-1; ++i) s[i] = next_var++;
        add_clause({-lits[0], s[0]});
        for (int i = 1; i < n-1; ++i) {
            add_clause({-s[i-1], s[i]});
            add_clause({-lits[i], s[i]});
            add_clause({-lits[i], -s[i-1]});
        }
        add_clause({-lits[n-1], -s[n-2]});
    }
    
    int run_propagation(double timeout_ms) {
        Solver solver(puzzle);
        solver.set_timeout(timeout_ms);
        auto result = solver.solve();
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c) {
                int8_t val = static_cast<int8_t>(result.get(r, c));
                grid[r * width + c] = val;
                if (val == 1) add_clause({cell_var[r][c]});
                else if (val == 0) add_clause({-cell_var[r][c]});
            }
        int unknowns = 0;
        for (int i = 0; i < width * height; ++i) if (grid[i] == 2) unknowns++;
        return unknowns;
    }
    
    void add_symmetry_constraints() {
        if (!has_col_symmetry) return;
        for (int r = 0; r < height; ++r) {
            for (int c = 0; c < width / 2; ++c) {
                int v1 = cell_var[r][c], v2 = cell_var[r][width - 1 - c];
                add_clause({-v1, v2});
                add_clause({v1, -v2});
            }
        }
    }
    
    void encode_line(const Description& desc, const std::vector<int>& cells, int len) {
        if (desc.empty()) { for (int c : cells) add_clause({-c}); return; }
        int ns = desc.size();
        std::vector<std::vector<int>> sv(ns);
        std::vector<int> mn(ns), mx(ns);
        int p = 0;
        for (int i = 0; i < ns; ++i) { mn[i] = p; p += desc[i] + 1; }
        p = len;
        for (int i = ns-1; i >= 0; --i) { p -= desc[i]; mx[i] = p; p--; }
        for (int i = 0; i < ns; ++i) {
            sv[i].resize(len, 0);
            for (int q = mn[i]; q <= mx[i]; ++q) sv[i][q] = next_var++;
        }
        for (int i = 0; i < ns; ++i) {
            std::vector<int> pos;
            for (int q = mn[i]; q <= mx[i]; ++q) pos.push_back(sv[i][q]);
            add_clause(pos);  // at-least-one
            at_most_one(pos);
        }
        for (int i = 0; i < ns-1; ++i)
            for (int q = mn[i]; q <= mx[i]; ++q) {
                int nm = q + desc[i] + 1;
                for (int q2 = mn[i+1]; q2 < nm && q2 <= mx[i+1]; ++q2)
                    add_clause({-sv[i][q], -sv[i+1][q2]});
            }
        for (int c = 0; c < len; ++c) {
            std::vector<int> cov;
            for (int i = 0; i < ns; ++i) {
                int pm = std::max(mn[i], c - desc[i] + 1);
                int px = std::min(mx[i], c);
                for (int q = pm; q <= px; ++q) if (sv[i][q]) cov.push_back(sv[i][q]);
            }
            if (cov.empty()) add_clause({-cells[c]});
            else {
                std::vector<int> cl = {-cells[c]};
                for (int v : cov) cl.push_back(v);
                add_clause(cl);
                for (int v : cov) add_clause({-v, cells[c]});
            }
        }
    }
    
    void encode() {
        add_symmetry_constraints();
        for (int r = 0; r < height; ++r) {
            std::vector<int> cells(width);
            for (int c = 0; c < width; ++c) cells[c] = cell_var[r][c];
            encode_line(puzzle.row_descriptions[r], cells, width);
        }
        for (int c = 0; c < width; ++c) {
            std::vector<int> cells(height);
            for (int r = 0; r < height; ++r) cells[r] = cell_var[r][c];
            encode_line(puzzle.col_descriptions[c], cells, height);
        }
    }
    
    bool run_sat(double& sat_time_ms) {
        std::string cnf_file = "/tmp/nono_fast.cnf";
        std::string out_file = "/tmp/nono_fast.out";
        
        std::ofstream out(cnf_file);
        out << "p cnf " << (next_var-1) << " " << clauses.size() << "\n";
        for (const auto& c : clauses) {
            for (int l : c) out << l << " ";
            out << "0\n";
        }
        out.close();
        
        // Adaptive solver selection
        std::string cmd;
        if (clauses.size() < 100000) {
            // Small problem: Kissat is fastest (no thread overhead)
            cmd = "kissat -q " + cnf_file + " > " + out_file + " 2>/dev/null";
        } else {
            // Large problem: CryptoMiniSat with parallelism
            int threads = std::thread::hardware_concurrency();
            cmd = "cryptominisat5 --verb 0 -t " + std::to_string(threads) + 
                  " " + cnf_file + " > " + out_file + " 2>/dev/null";
        }
        
        auto t1 = std::chrono::high_resolution_clock::now();
        system(cmd.c_str());
        auto t2 = std::chrono::high_resolution_clock::now();
        sat_time_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        
        // Parse result (handles both Kissat and CMS output formats)
        std::ifstream result_file(out_file);
        std::string line;
        bool sat = false;
        std::vector<bool> assignments(next_var, false);
        
        while (std::getline(result_file, line)) {
            if (line.find("SATISFIABLE") != std::string::npos) sat = true;
            else if (line[0] == 'v') {
                std::istringstream iss(line.substr(2));
                int lit;
                while (iss >> lit && lit != 0) {
                    if (lit > 0 && lit < next_var) assignments[lit] = true;
                    else if (lit < 0 && -lit < next_var) assignments[-lit] = false;
                }
            }
        }
        
        if (!sat) return false;
        
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < width; ++c)
                grid[r * width + c] = assignments[cell_var[r][c]] ? 1 : 0;
        
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
    std::string filename = argc > 1 ? argv[1] : "webpbn_puzzles/8098.non";
    double prop_timeout = argc > 2 ? std::stod(argv[2]) : 2000;
    
    std::ifstream file(filename);
    if (!file) { std::cerr << "Cannot open: " << filename << "\n"; return 1; }
    std::stringstream buf;
    buf << file.rdbuf();
    
    auto info = parse_non_file_with_info(buf.str());
    if (!info) { std::cerr << "Parse failed\n"; return 1; }
    
    std::cout << "Puzzle: " << info->title << " (" << info->puzzle.width << "x" << info->puzzle.height << ")\n";
    
    FastNonogramSolver solver(info->puzzle);
    auto t_start = std::chrono::high_resolution_clock::now();
    
    int unknowns = solver.run_propagation(prop_timeout);
    std::cout << "After propagation: " << unknowns << " unknowns\n";
    
    if (unknowns == 0) {
        auto t_end = std::chrono::high_resolution_clock::now();
        std::cout << "SOLVED by propagation in " 
                  << std::chrono::duration<double, std::milli>(t_end - t_start).count() << "ms\n";
        solver.print_grid();
        return 0;
    }
    
    solver.encode();
    std::cout << "CNF: " << solver.next_var-1 << " vars, " << solver.clauses.size() << " clauses";
    std::cout << (solver.has_col_symmetry ? " (symmetric)" : "") << "\n";
    std::cout << "Solver: " << (solver.clauses.size() < 100000 ? "Kissat" : "CryptoMiniSat") << "\n";
    
    double sat_ms;
    bool solved = solver.run_sat(sat_ms);
    
    auto t_end = std::chrono::high_resolution_clock::now();
    double total = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    
    std::cout << (solved ? "SOLVED" : "FAILED") << " in " << total << "ms (SAT: " << sat_ms << "ms)\n";
    if (solved && info->puzzle.width <= 50) solver.print_grid();
    
    return solved ? 0 : 1;
}
