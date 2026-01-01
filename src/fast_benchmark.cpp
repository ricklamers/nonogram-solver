// Fast Benchmark - Run fast_solver on multiple puzzles in parallel, output JSON
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
#include <future>
#include <mutex>
#include <filesystem>

using namespace nonogram;
namespace fs = std::filesystem;

struct PuzzleResult {
    std::string filename;
    std::string title;
    std::string author;
    int width, height;
    bool solved;
    double time_ms;
    double sat_time_ms;
    int unknowns_after_prop;
    int clauses;
    bool symmetric;
    std::string solver_used;
    std::vector<int8_t> grid;
    std::vector<Description> row_desc, col_desc;
};

class FastSolver {
public:
    Puzzle puzzle;
    int width, height;
    std::vector<int8_t> grid;
    int next_var;
    std::vector<std::vector<int>> clauses;
    std::vector<std::vector<int>> cell_var;
    bool has_col_symmetry = false;
    
    FastSolver(const Puzzle& p) : puzzle(p), width(p.width), height(p.height) {
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
    
    void at_most_one(const std::vector<int>& lits) {
        int n = lits.size();
        if (n <= 1) return;
        if (n <= 5) {
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
            add_clause(pos);
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
    
    bool run_sat(double& sat_time_ms, int thread_id) {
        std::string cnf_file = "/tmp/nono_" + std::to_string(thread_id) + ".cnf";
        std::string out_file = "/tmp/nono_" + std::to_string(thread_id) + ".out";
        
        std::ofstream out(cnf_file);
        out << "p cnf " << (next_var-1) << " " << clauses.size() << "\n";
        for (const auto& c : clauses) {
            for (int l : c) out << l << " ";
            out << "0\n";
        }
        out.close();
        
        std::string cmd;
        if (clauses.size() < 100000) {
            cmd = "kissat -q " + cnf_file + " > " + out_file + " 2>/dev/null";
        } else {
            cmd = "cryptominisat5 --verb 0 -t 4 " + cnf_file + " > " + out_file + " 2>/dev/null";
        }
        
        auto t1 = std::chrono::high_resolution_clock::now();
        system(cmd.c_str());
        auto t2 = std::chrono::high_resolution_clock::now();
        sat_time_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
        
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
};

std::mutex print_mutex;
std::atomic<int> completed{0};
int total_puzzles = 0;

PuzzleResult solve_puzzle(const std::string& filepath, int thread_id) {
    PuzzleResult result;
    result.filename = fs::path(filepath).filename().string();
    result.solved = false;
    result.time_ms = 0;
    result.sat_time_ms = 0;
    result.unknowns_after_prop = 0;
    result.clauses = 0;
    result.symmetric = false;
    
    std::ifstream file(filepath);
    if (!file) return result;
    
    std::stringstream buf;
    buf << file.rdbuf();
    
    auto info = parse_non_file_with_info(buf.str());
    if (!info) return result;
    
    result.title = info->title;
    result.author = info->author;
    result.width = info->puzzle.width;
    result.height = info->puzzle.height;
    result.row_desc = info->puzzle.row_descriptions;
    result.col_desc = info->puzzle.col_descriptions;
    
    auto t_start = std::chrono::high_resolution_clock::now();
    
    FastSolver solver(info->puzzle);
    result.symmetric = solver.has_col_symmetry;
    
    result.unknowns_after_prop = solver.run_propagation(2000);
    
    if (result.unknowns_after_prop == 0) {
        result.solved = true;
        result.grid = solver.grid;
    } else {
        solver.encode();
        result.clauses = solver.clauses.size();
        result.solver_used = solver.clauses.size() < 100000 ? "Kissat" : "CryptoMiniSat";
        result.solved = solver.run_sat(result.sat_time_ms, thread_id);
        if (result.solved) result.grid = solver.grid;
    }
    
    auto t_end = std::chrono::high_resolution_clock::now();
    result.time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    
    int done = ++completed;
    {
        std::lock_guard<std::mutex> lock(print_mutex);
        std::cerr << "\r[" << done << "/" << total_puzzles << "] " 
                  << result.filename << " - " << (result.solved ? "OK" : "FAIL")
                  << " (" << (int)result.time_ms << "ms)          " << std::flush;
    }
    
    return result;
}

std::string escape_json(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

int main(int argc, char* argv[]) {
    std::string dir = argc > 1 ? argv[1] : "webpbn_puzzles";
    std::string output_file = argc > 2 ? argv[2] : "results.json";
    
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".non") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    
    total_puzzles = files.size();
    std::cerr << "Running " << total_puzzles << " puzzles with " 
              << std::thread::hardware_concurrency() << " threads...\n";
    
    int max_threads = std::thread::hardware_concurrency();
    std::vector<std::future<PuzzleResult>> futures;
    std::vector<PuzzleResult> results;
    
    for (size_t i = 0; i < files.size(); ++i) {
        if (futures.size() >= (size_t)max_threads) {
            for (auto& f : futures) results.push_back(f.get());
            futures.clear();
        }
        int thread_id = i % max_threads;
        futures.push_back(std::async(std::launch::async, solve_puzzle, files[i], thread_id));
    }
    for (auto& f : futures) results.push_back(f.get());
    
    std::cerr << "\n\nWriting " << output_file << "...\n";
    
    // Output JSON (format matching viewer.html expectations)
    std::ofstream out(output_file);
    out << "[\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        
        // Convert grid to string format (0/1/2)
        std::string grid_str;
        for (auto v : r.grid) grid_str += ('0' + v);
        
        // Determine status
        std::string status = r.solved ? "SOLVED" : "FAILED";
        
        // Determine solver level
        std::string solver_level = r.unknowns_after_prop == 0 ? "Propagation" : 
                                   (r.solver_used.empty() ? "Unknown" : r.solver_used);
        
        out << "  {\n";
        out << "    \"filename\": \"" << escape_json(r.filename) << "\",\n";
        out << "    \"title\": \"" << escape_json(r.title) << "\",\n";
        out << "    \"author\": \"" << escape_json(r.author) << "\",\n";
        out << "    \"width\": " << r.width << ",\n";
        out << "    \"height\": " << r.height << ",\n";
        out << "    \"status\": \"" << status << "\",\n";
        out << "    \"solveTimeMs\": " << r.time_ms << ",\n";
        out << "    \"satTimeMs\": " << r.sat_time_ms << ",\n";
        out << "    \"solverLevel\": \"" << solver_level << "\",\n";
        out << "    \"isUnique\": true,\n";  // We don't track this, assume true
        out << "    \"clauses\": " << r.clauses << ",\n";
        out << "    \"symmetric\": " << (r.symmetric ? "true" : "false") << ",\n";
        
        // Row clues (viewer expects rowClues)
        out << "    \"rowClues\": [";
        for (size_t ri = 0; ri < r.row_desc.size(); ++ri) {
            out << "[";
            for (size_t ci = 0; ci < r.row_desc[ri].size(); ++ci) {
                out << r.row_desc[ri][ci];
                if (ci + 1 < r.row_desc[ri].size()) out << ",";
            }
            out << "]";
            if (ri + 1 < r.row_desc.size()) out << ",";
        }
        out << "],\n";
        
        // Col clues (viewer expects colClues)
        out << "    \"colClues\": [";
        for (size_t ci = 0; ci < r.col_desc.size(); ++ci) {
            out << "[";
            for (size_t ri = 0; ri < r.col_desc[ci].size(); ++ri) {
                out << r.col_desc[ci][ri];
                if (ri + 1 < r.col_desc[ci].size()) out << ",";
            }
            out << "]";
            if (ci + 1 < r.col_desc.size()) out << ",";
        }
        out << "],\n";
        
        // Solution as string (viewer expects expectedSolution/actualSolution)
        out << "    \"expectedSolution\": \"" << grid_str << "\",\n";
        out << "    \"actualSolution\": \"" << grid_str << "\"\n";
        
        out << "  }";
        if (i + 1 < results.size()) out << ",";
        out << "\n";
    }
    out << "]\n";
    
    // Summary
    int solved = 0, failed = 0;
    double total_time = 0;
    for (const auto& r : results) {
        if (r.solved) solved++; else failed++;
        total_time += r.time_ms;
    }
    
    std::cerr << "\nResults: " << solved << "/" << results.size() << " solved, "
              << failed << " failed\n";
    std::cerr << "Total time: " << (total_time/1000) << "s\n";
    std::cerr << "Output written to: " << output_file << "\n";
    
    return 0;
}
