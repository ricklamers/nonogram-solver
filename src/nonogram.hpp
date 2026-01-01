#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <atomic>
#include <chrono>

namespace nonogram {

// Pixel values: 0 = white, 1 = black, 2 = undecided
enum class Pixel : int8_t {
    WHITE = 0,
    BLACK = 1,
    UNDECIDED = 2
};

// A line description is a sequence of segment lengths
using Description = std::vector<int>;

// A line is a sequence of pixels
using Line = std::vector<Pixel>;

// 2-SAT literal: (pixel_index, is_black)
// If is_black is true, the literal represents "pixel must be black"
// If is_black is false, the literal represents "pixel must be white"
struct Literal {
    int pixel_index;
    bool is_black;
    
    Literal negate() const { return {pixel_index, !is_black}; }
    
    bool operator==(const Literal& other) const {
        return pixel_index == other.pixel_index && is_black == other.is_black;
    }
};

// 2-SAT clause: (a -> b), meaning if a is true, then b must be true
// This can also be written as (!a OR b)
struct Clause {
    Literal premise;
    Literal conclusion;
};

// Nonogram puzzle definition
struct Puzzle {
    int width;
    int height;
    std::vector<Description> row_descriptions;
    std::vector<Description> col_descriptions;
    std::string expected_solution;  // For testing, optional
};

// Solver result
struct SolverResult {
    std::vector<Pixel> grid;  // Row-major order
    int width;
    int height;
    int unknowns;  // Number of undecided pixels
    bool is_unique;  // True if solution is unique
    bool is_solved;  // True if all pixels determined
    bool timed_out;  // True if solver timed out
    double solve_time_ms;  // Time taken to solve
    std::string solver_level;  // "simple", "0-solvable", "1-solvable", "partial", or "timeout"
    
    Pixel get(int row, int col) const {
        return grid[row * width + col];
    }
    
    void set(int row, int col, Pixel value) {
        grid[row * width + col] = value;
    }
    
    std::string to_string() const;
    bool matches_solution(const std::string& expected) const;
    
    // Validate that solution satisfies all row and column descriptions
    bool validate(const std::vector<Description>& row_descs, 
                  const std::vector<Description>& col_descs) const;
};

// Parse puzzle from text format (nono-bench style)
Puzzle parse_puzzle(const std::string& clues_text, int width, int height, 
                    const std::string& expected_solution = "");

// Parse puzzle from .non file format (nonogram-db style)
// Returns empty optional if parsing failed
std::optional<Puzzle> parse_non_file(const std::string& content);

// Load puzzle from a .non file path
std::optional<Puzzle> load_non_file(const std::string& filepath);

// Extended puzzle info (for display/reporting)
struct PuzzleInfo {
    Puzzle puzzle;
    std::string title;
    std::string author;
    std::string catalogue;
    std::string filepath;
};

// Parse .non file with metadata
std::optional<PuzzleInfo> parse_non_file_with_info(const std::string& content);

// Main solver class
class Solver {
public:
    explicit Solver(const Puzzle& puzzle);
    
    // Set timeout in milliseconds (0 = no timeout)
    void set_timeout(double timeout_ms) { timeout_ms_ = timeout_ms; }
    
    // Solve the puzzle, returns result
    SolverResult solve();
    
    // Get current grid state
    const std::vector<Pixel>& grid() const { return grid_; }
    
private:
    Puzzle puzzle_;
    std::vector<Pixel> grid_;  // Row-major order
    int width_, height_;
    
    // Timeout support
    double timeout_ms_ = 0;  // 0 = no timeout
    std::chrono::steady_clock::time_point start_time_;
    bool timed_out_ = false;
    
    // Check if we've exceeded timeout
    bool check_timeout() {
        if (timeout_ms_ <= 0) return false;
        if (timed_out_) return true;
        auto elapsed = std::chrono::steady_clock::now() - start_time_;
        auto elapsed_ms = std::chrono::duration<double, std::milli>(elapsed).count();
        if (elapsed_ms >= timeout_ms_) {
            timed_out_ = true;
            return true;
        }
        return false;
    }
    
    // Helper to get pixel index
    int pixel_index(int row, int col) const { return row * width_ + col; }
    
    // Extract a row or column from the grid
    Line get_row(int row) const;
    Line get_col(int col) const;
    
    // Set a row or column in the grid
    void set_row(int row, const Line& line);
    void set_col(int col, const Line& line);
    
    // Core operations from the paper
    
    // Check if a line (with potential undecided pixels) is fixable with respect to description
    // Returns empty if not fixable, otherwise returns the settled line
    std::optional<Line> settle(const Line& line, const Description& desc);
    
    // Apply Settle to all rows and columns until no progress
    // Returns true if any changes were made
    bool full_settle();
    
    // Collect 2-SAT clauses from line relaxations
    std::vector<Clause> collect_clauses();
    
    // Collect clauses for a single line
    std::vector<Clause> collect_line_clauses(const Line& line, const Description& desc,
                                              const std::vector<int>& pixel_indices);
    
    // Solve 2-SAT and fix any determined pixels
    // Returns number of pixels fixed
    int solve_2sat(const std::vector<Clause>& clauses);
    
    // Build and analyze dependency graph for 2-SAT
    std::vector<int> find_forced_values(const std::vector<Clause>& clauses, int num_pixels);
    
    // Solver0: FullSettle + 2SAT until no progress
    bool solver0();
    
    // Solver1: Solver0 + single guessing
    bool solver1();
    
    // Complete the solution by making arbitrary choices for remaining unknowns
    // Returns true if a valid complete solution was found
    bool complete_solution();
    bool complete_solution_recursive(int start_idx);
    
    // Count undecided pixels
    int count_unknowns() const;
    
    // Check if grid is completely solved
    bool is_solved() const;
    
    // Check for contradictions
    bool has_contradiction() const;
    
    // Dynamic programming for line fixability
    // dp[i][j] = true if prefix of length i of line is fixable with first j segments
    bool compute_fixability(const Line& line, const Description& desc,
                            std::vector<std::vector<bool>>& dp);
};

} // namespace nonogram
