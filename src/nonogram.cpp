#include "nonogram.hpp"
#include <algorithm>
#include <sstream>
#include <regex>
#include <chrono>
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <iostream>
#include <fstream>

namespace nonogram {

// Helper: trim whitespace from string
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Helper: remove quotes from a string value
static std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

// Helper: parse a comma-separated clue line like "2,1,3" or "2" or "0" (empty) or empty line
static Description parse_clue_line(const std::string& line) {
    Description desc;
    std::string trimmed = trim(line);
    if (trimmed.empty()) {
        return desc;  // Empty description (all white)
    }
    
    // Handle explicit "0" meaning empty line (all white)
    if (trimmed == "0") {
        return desc;  // Return empty description
    }
    
    std::istringstream iss(trimmed);
    std::string token;
    while (std::getline(iss, token, ',')) {
        // Extract only the numeric part (ignore color suffixes like "3b")
        std::string num_str;
        for (char c : token) {
            if (std::isdigit(c)) {
                num_str += c;
            } else {
                break;  // Stop at first non-digit (color designation)
            }
        }
        if (!num_str.empty()) {
            int val = std::stoi(num_str);
            // Skip zeros within the clue (shouldn't happen in valid puzzles, but handle it)
            if (val > 0) {
                desc.push_back(val);
            }
        }
    }
    return desc;
}

std::optional<PuzzleInfo> parse_non_file_with_info(const std::string& content) {
    PuzzleInfo info;
    Puzzle& puzzle = info.puzzle;
    
    std::istringstream iss(content);
    std::string line;
    
    int width = 0, height = 0;
    bool in_rows = false, in_cols = false;
    int row_count = 0, col_count = 0;
    
    while (std::getline(iss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        
        // Parse key-value pairs
        if (trimmed.find("width ") == 0) {
            width = std::stoi(trimmed.substr(6));
            in_rows = false;
            in_cols = false;
        }
        else if (trimmed.find("height ") == 0) {
            height = std::stoi(trimmed.substr(7));
            in_rows = false;
            in_cols = false;
        }
        else if (trimmed.find("title ") == 0) {
            info.title = unquote(trim(trimmed.substr(6)));
            in_rows = false;
            in_cols = false;
        }
        else if (trimmed.find("by ") == 0) {
            info.author = unquote(trim(trimmed.substr(3)));
            in_rows = false;
            in_cols = false;
        }
        else if (trimmed.find("catalogue ") == 0) {
            info.catalogue = unquote(trim(trimmed.substr(10)));
            in_rows = false;
            in_cols = false;
        }
        else if (trimmed.find("goal ") == 0) {
            puzzle.expected_solution = unquote(trim(trimmed.substr(5)));
            in_rows = false;
            in_cols = false;
        }
        else if (trimmed == "rows") {
            in_rows = true;
            in_cols = false;
            puzzle.row_descriptions.clear();
            puzzle.row_descriptions.resize(height);
            row_count = 0;
        }
        else if (trimmed == "columns") {
            in_rows = false;
            in_cols = true;
            puzzle.col_descriptions.clear();
            puzzle.col_descriptions.resize(width);
            col_count = 0;
        }
        else if (in_rows && row_count < height) {
            puzzle.row_descriptions[row_count] = parse_clue_line(trimmed);
            row_count++;
        }
        else if (in_cols && col_count < width) {
            puzzle.col_descriptions[col_count] = parse_clue_line(trimmed);
            col_count++;
        }
        // Skip other keys (license, copyright, color, etc.)
    }
    
    // Validate
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    if ((int)puzzle.row_descriptions.size() != height || 
        (int)puzzle.col_descriptions.size() != width) {
        return std::nullopt;
    }
    
    puzzle.width = width;
    puzzle.height = height;
    
    return info;
}

std::optional<Puzzle> parse_non_file(const std::string& content) {
    auto info = parse_non_file_with_info(content);
    if (!info) return std::nullopt;
    return info->puzzle;
}

std::optional<Puzzle> load_non_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file) {
        return std::nullopt;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    return parse_non_file(buffer.str());
}

std::string SolverResult::to_string() const {
    std::string result;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            Pixel p = get(row, col);
            if (p == Pixel::BLACK) result += '1';
            else if (p == Pixel::WHITE) result += '0';
            else result += 'x';
        }
    }
    return result;
}

bool SolverResult::matches_solution(const std::string& expected) const {
    if (expected.empty()) return true;
    for (int i = 0; i < width * height && i < (int)expected.size(); ++i) {
        if (grid[i] == Pixel::UNDECIDED) continue;
        char expected_char = expected[i];
        if (expected_char == '1' && grid[i] != Pixel::BLACK) return false;
        if (expected_char == '0' && grid[i] != Pixel::WHITE) return false;
    }
    return true;
}

// Helper: check if a line matches a description
static bool line_matches_description(const std::vector<Pixel>& line, const Description& desc) {
    // Extract run lengths of black pixels
    std::vector<int> runs;
    int current_run = 0;
    for (Pixel p : line) {
        if (p == Pixel::BLACK) {
            current_run++;
        } else {
            if (current_run > 0) {
                runs.push_back(current_run);
                current_run = 0;
            }
        }
    }
    if (current_run > 0) {
        runs.push_back(current_run);
    }
    
    return runs == desc;
}

bool SolverResult::validate(const std::vector<Description>& row_descs,
                            const std::vector<Description>& col_descs) const {
    if (!is_solved) return false;  // Can't validate incomplete solution
    
    // Check all rows
    for (int row = 0; row < height; ++row) {
        std::vector<Pixel> line(width);
        for (int col = 0; col < width; ++col) {
            line[col] = get(row, col);
        }
        if (!line_matches_description(line, row_descs[row])) {
            return false;
        }
    }
    
    // Check all columns
    for (int col = 0; col < width; ++col) {
        std::vector<Pixel> line(height);
        for (int row = 0; row < height; ++row) {
            line[row] = get(row, col);
        }
        if (!line_matches_description(line, col_descs[col])) {
            return false;
        }
    }
    
    return true;
}

Puzzle parse_puzzle(const std::string& clues_text, int width, int height,
                    const std::string& expected_solution) {
    Puzzle puzzle;
    puzzle.width = width;
    puzzle.height = height;
    puzzle.expected_solution = expected_solution;
    puzzle.row_descriptions.resize(height);
    puzzle.col_descriptions.resize(width);
    
    std::istringstream stream(clues_text);
    std::string line;
    bool reading_rows = false;
    bool reading_cols = false;
    
    std::regex row_pattern(R"(Row\s+(\d+):\s*(.*))");
    std::regex col_pattern(R"(Column\s+(\d+):\s*(.*))");
    
    while (std::getline(stream, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        if (line.find("Row clues") != std::string::npos) {
            reading_rows = true;
            reading_cols = false;
            continue;
        }
        if (line.find("Column clues") != std::string::npos) {
            reading_rows = false;
            reading_cols = true;
            continue;
        }
        
        std::smatch match;
        if (reading_rows && std::regex_search(line, match, row_pattern)) {
            int row_num = std::stoi(match[1].str()) - 1;  // 1-indexed to 0-indexed
            std::string nums = match[2].str();
            
            if (row_num >= 0 && row_num < height) {
                Description desc;
                std::istringstream nums_stream(nums);
                int num;
                while (nums_stream >> num) {
                    desc.push_back(num);
                }
                puzzle.row_descriptions[row_num] = desc;
            }
        }
        else if (reading_cols && std::regex_search(line, match, col_pattern)) {
            int col_num = std::stoi(match[1].str()) - 1;  // 1-indexed to 0-indexed
            std::string nums = match[2].str();
            
            if (col_num >= 0 && col_num < width) {
                Description desc;
                std::istringstream nums_stream(nums);
                int num;
                while (nums_stream >> num) {
                    desc.push_back(num);
                }
                puzzle.col_descriptions[col_num] = desc;
            }
        }
    }
    
    return puzzle;
}

Solver::Solver(const Puzzle& puzzle) 
    : puzzle_(puzzle), width_(puzzle.width), height_(puzzle.height) {
    grid_.resize(width_ * height_, Pixel::UNDECIDED);
}

Line Solver::get_row(int row) const {
    Line result(width_);
    for (int col = 0; col < width_; ++col) {
        result[col] = grid_[pixel_index(row, col)];
    }
    return result;
}

Line Solver::get_col(int col) const {
    Line result(height_);
    for (int row = 0; row < height_; ++row) {
        result[row] = grid_[pixel_index(row, col)];
    }
    return result;
}

void Solver::set_row(int row, const Line& line) {
    for (int col = 0; col < width_; ++col) {
        grid_[pixel_index(row, col)] = line[col];
    }
}

void Solver::set_col(int col, const Line& line) {
    for (int row = 0; row < height_; ++row) {
        grid_[pixel_index(row, col)] = line[row];
    }
}

int Solver::count_unknowns() const {
    return std::count(grid_.begin(), grid_.end(), Pixel::UNDECIDED);
}

bool Solver::is_solved() const {
    return count_unknowns() == 0;
}

// Dynamic programming to check if a line is fixable and compute settled values
// Based on Proposition 1 from the paper
bool Solver::compute_fixability(const Line& line, const Description& desc,
                                 std::vector<std::vector<bool>>& dp) {
    int n = line.size();
    int m = desc.size();
    
    // dp[i][j] = true if line[0..i-1] can be fixed to satisfy first j segments
    // with position i being just after those j segments are complete
    dp.assign(n + 1, std::vector<bool>(m + 1, false));
    dp[0][0] = true;  // Empty prefix with no segments
    
    // Handle empty description (all white)
    if (m == 0) {
        bool can_be_all_white = true;
        for (int i = 0; i < n; ++i) {
            if (line[i] == Pixel::BLACK) {
                can_be_all_white = false;
                break;
            }
        }
        dp[n][0] = can_be_all_white;
        return can_be_all_white;
    }
    
    // For each position i and segment count j
    for (int i = 0; i <= n; ++i) {
        for (int j = 0; j <= m; ++j) {
            if (!dp[i][j]) continue;
            
            // Option 1: Place a white pixel at position i (if we have room)
            if (i < n && line[i] != Pixel::BLACK) {
                dp[i + 1][j] = true;
            }
            
            // Option 2: Place segment j at positions [i, i + desc[j] - 1]
            if (j < m) {
                int seg_len = desc[j];
                if (i + seg_len > n) continue;  // Not enough space
                
                // Check if we can place a segment here
                bool can_place = true;
                for (int k = i; k < i + seg_len; ++k) {
                    if (line[k] == Pixel::WHITE) {
                        can_place = false;
                        break;
                    }
                }
                
                if (can_place) {
                    // After the segment, we need either end of line or a white pixel
                    int after = i + seg_len;
                    if (after == n) {
                        // Segment ends exactly at end of line
                        dp[after][j + 1] = true;
                    } else if (line[after] != Pixel::BLACK) {
                        // Need at least one white after segment
                        dp[after + 1][j + 1] = true;
                    }
                }
            }
        }
    }
    
    return dp[n][m];
}

std::optional<Line> Solver::settle(const Line& line, const Description& desc) {
    int n = line.size();
    
    // First check if the line is fixable
    std::vector<std::vector<bool>> dp_forward;
    if (!compute_fixability(line, desc, dp_forward)) {
        return std::nullopt;  // Not fixable
    }
    
    Line result = line;
    
    // Simple approach: test each undecided pixel individually
    // This is O(n^2 * m) but more reliable
    for (int i = 0; i < n; ++i) {
        if (line[i] != Pixel::UNDECIDED) continue;
        
        bool can_be_white = false;
        bool can_be_black = false;
        
        // Try placing white at position i
        {
            Line test_line = line;
            test_line[i] = Pixel::WHITE;
            std::vector<std::vector<bool>> test_dp;
            can_be_white = compute_fixability(test_line, desc, test_dp);
        }
        
        // Try placing black at position i
        {
            Line test_line = line;
            test_line[i] = Pixel::BLACK;
            std::vector<std::vector<bool>> test_dp;
            can_be_black = compute_fixability(test_line, desc, test_dp);
        }
        
        if (!can_be_white && !can_be_black) {
            return std::nullopt;  // Contradiction
        } else if (can_be_white && !can_be_black) {
            result[i] = Pixel::WHITE;
        } else if (!can_be_white && can_be_black) {
            result[i] = Pixel::BLACK;
        }
        // else: both possible, leave as undecided
    }
    
    return result;
}

bool Solver::full_settle() {
    bool made_progress;
    
    do {
        made_progress = false;
        
        // Process all rows
        for (int row = 0; row < height_; ++row) {
            if (check_timeout()) return true;  // Return true to avoid marking as contradiction
            
            Line line = get_row(row);
            auto settled = settle(line, puzzle_.row_descriptions[row]);
            
            if (!settled) {
                // Contradiction detected
                return false;
            }
            
            for (int col = 0; col < width_; ++col) {
                if (line[col] == Pixel::UNDECIDED && (*settled)[col] != Pixel::UNDECIDED) {
                    made_progress = true;
                }
            }
            set_row(row, *settled);
        }
        
        // Process all columns
        for (int col = 0; col < width_; ++col) {
            if (check_timeout()) return true;
            
            Line line = get_col(col);
            auto settled = settle(line, puzzle_.col_descriptions[col]);
            
            if (!settled) {
                // Contradiction detected
                return false;
            }
            
            for (int row = 0; row < height_; ++row) {
                if (line[row] == Pixel::UNDECIDED && (*settled)[row] != Pixel::UNDECIDED) {
                    made_progress = true;
                }
            }
            set_col(col, *settled);
        }
        
    } while (made_progress && !timed_out_);
    
    return true;
}

bool Solver::has_contradiction() const {
    // Check each row
    for (int row = 0; row < height_; ++row) {
        Line line = get_row(row);
        std::vector<std::vector<bool>> dp;
        if (!const_cast<Solver*>(this)->compute_fixability(line, puzzle_.row_descriptions[row], dp)) {
            return true;
        }
    }
    
    // Check each column
    for (int col = 0; col < width_; ++col) {
        Line line = get_col(col);
        std::vector<std::vector<bool>> dp;
        if (!const_cast<Solver*>(this)->compute_fixability(line, puzzle_.col_descriptions[col], dp)) {
            return true;
        }
    }
    
    return false;
}

std::vector<Clause> Solver::collect_line_clauses(const Line& line, const Description& desc,
                                                   const std::vector<int>& pixel_indices) {
    std::vector<Clause> clauses;
    
    // Find undecided positions
    std::vector<int> undecided_positions;
    for (int i = 0; i < (int)line.size(); ++i) {
        if (line[i] == Pixel::UNDECIDED) {
            undecided_positions.push_back(i);
        }
    }
    
    // For each pair of undecided pixels, test all 4 assignments
    for (size_t i = 0; i < undecided_positions.size(); ++i) {
        for (size_t j = i + 1; j < undecided_positions.size(); ++j) {
            int pos_a = undecided_positions[i];
            int pos_b = undecided_positions[j];
            int idx_a = pixel_indices[pos_a];
            int idx_b = pixel_indices[pos_b];
            
            // Test all 4 combinations: (0,0), (0,1), (1,0), (1,1)
            for (int val_a = 0; val_a <= 1; ++val_a) {
                for (int val_b = 0; val_b <= 1; ++val_b) {
                    Line test_line = line;
                    test_line[pos_a] = (val_a == 0) ? Pixel::WHITE : Pixel::BLACK;
                    test_line[pos_b] = (val_b == 0) ? Pixel::WHITE : Pixel::BLACK;
                    
                    std::vector<std::vector<bool>> dp;
                    if (!compute_fixability(test_line, desc, dp)) {
                        // This combination is impossible
                        // If a=val_a AND b=val_b leads to contradiction, then
                        // (a=val_a) implies (b != val_b), i.e., (a=val_a) -> (b=!val_b)
                        Literal premise = {idx_a, val_a == 1};
                        Literal conclusion = {idx_b, val_b != 1};
                        clauses.push_back({premise, conclusion});
                        
                        // Also: (b=val_b) -> (a != val_a)
                        Literal premise2 = {idx_b, val_b == 1};
                        Literal conclusion2 = {idx_a, val_a != 1};
                        clauses.push_back({premise2, conclusion2});
                    }
                }
            }
        }
    }
    
    return clauses;
}

std::vector<Clause> Solver::collect_clauses() {
    std::vector<Clause> all_clauses;
    
    // Collect from rows
    for (int row = 0; row < height_; ++row) {
        Line line = get_row(row);
        std::vector<int> indices(width_);
        for (int col = 0; col < width_; ++col) {
            indices[col] = pixel_index(row, col);
        }
        auto clauses = collect_line_clauses(line, puzzle_.row_descriptions[row], indices);
        all_clauses.insert(all_clauses.end(), clauses.begin(), clauses.end());
    }
    
    // Collect from columns
    for (int col = 0; col < width_; ++col) {
        Line line = get_col(col);
        std::vector<int> indices(height_);
        for (int row = 0; row < height_; ++row) {
            indices[row] = pixel_index(row, col);
        }
        auto clauses = collect_line_clauses(line, puzzle_.col_descriptions[col], indices);
        all_clauses.insert(all_clauses.end(), clauses.begin(), clauses.end());
    }
    
    return all_clauses;
}

std::vector<int> Solver::find_forced_values(const std::vector<Clause>& clauses, int num_pixels) {
    // Build implication graph
    // For each pixel i, we have two nodes: 2*i (pixel is white) and 2*i+1 (pixel is black)
    // An edge from A to B means "if A is true, then B must be true"
    
    int num_nodes = 2 * num_pixels;
    std::vector<std::vector<int>> adj(num_nodes);
    
    auto literal_to_node = [](const Literal& lit) -> int {
        return 2 * lit.pixel_index + (lit.is_black ? 1 : 0);
    };
    
    for (const auto& clause : clauses) {
        int from = literal_to_node(clause.premise);
        int to = literal_to_node(clause.conclusion);
        adj[from].push_back(to);
    }
    
    // For each undecided pixel, check if there's a path from one value to the other
    // If there's a path from "black" to "white", the pixel must be white
    // If there's a path from "white" to "black", the pixel must be black
    
    std::vector<int> result(num_pixels, -1);  // -1 = undetermined
    
    for (int pixel = 0; pixel < num_pixels; ++pixel) {
        if (grid_[pixel] != Pixel::UNDECIDED) continue;
        
        int white_node = 2 * pixel;
        int black_node = 2 * pixel + 1;
        
        // BFS from white_node to see if we can reach black_node
        std::vector<bool> visited(num_nodes, false);
        std::queue<int> q;
        q.push(white_node);
        visited[white_node] = true;
        bool white_implies_black = false;
        
        while (!q.empty()) {
            int cur = q.front();
            q.pop();
            if (cur == black_node) {
                white_implies_black = true;
                break;
            }
            for (int next : adj[cur]) {
                if (!visited[next]) {
                    visited[next] = true;
                    q.push(next);
                }
            }
        }
        
        // BFS from black_node to see if we can reach white_node
        std::fill(visited.begin(), visited.end(), false);
        q = std::queue<int>();
        q.push(black_node);
        visited[black_node] = true;
        bool black_implies_white = false;
        
        while (!q.empty()) {
            int cur = q.front();
            q.pop();
            if (cur == white_node) {
                black_implies_white = true;
                break;
            }
            for (int next : adj[cur]) {
                if (!visited[next]) {
                    visited[next] = true;
                    q.push(next);
                }
            }
        }
        
        if (white_implies_black && black_implies_white) {
            // Contradiction - this shouldn't happen if puzzle is solvable
            result[pixel] = -2;  // Mark as contradiction
        } else if (white_implies_black) {
            result[pixel] = 1;  // Must be black
        } else if (black_implies_white) {
            result[pixel] = 0;  // Must be white
        }
    }
    
    return result;
}

int Solver::solve_2sat(const std::vector<Clause>& clauses) {
    int num_pixels = width_ * height_;
    auto forced = find_forced_values(clauses, num_pixels);
    
    int fixed_count = 0;
    for (int i = 0; i < num_pixels; ++i) {
        if (grid_[i] == Pixel::UNDECIDED) {
            if (forced[i] == 0) {
                grid_[i] = Pixel::WHITE;
                fixed_count++;
            } else if (forced[i] == 1) {
                grid_[i] = Pixel::BLACK;
                fixed_count++;
            }
        }
    }
    
    return fixed_count;
}

bool Solver::solver0() {
    bool made_progress;
    do {
        if (check_timeout()) return true;
        
        made_progress = false;
        
        // Run FullSettle
        if (!full_settle()) {
            return false;  // Contradiction
        }
        
        if (is_solved() || timed_out_) return true;
        
        // Collect and solve 2-SAT
        auto clauses = collect_clauses();
        int fixed = solve_2sat(clauses);
        if (fixed > 0) {
            made_progress = true;
        }
        
    } while (made_progress && !timed_out_);
    
    return true;
}

bool Solver::solver1() {
    // First run Solver0
    if (!solver0()) {
        return false;
    }
    
    if (is_solved() || timed_out_) return true;
    
    // Collect pixels that appear in 2-SAT clauses - these are more likely to be determinable
    auto clauses = collect_clauses();
    std::unordered_set<int> clause_pixels;
    for (const auto& clause : clauses) {
        clause_pixels.insert(clause.premise.pixel_index);
        clause_pixels.insert(clause.conclusion.pixel_index);
    }
    
    // Sort pixels by how many clauses they appear in (more constrained = try first)
    std::vector<std::pair<int, int>> pixel_counts;
    for (int pixel : clause_pixels) {
        if (grid_[pixel] == Pixel::UNDECIDED) {
            int count = 0;
            for (const auto& clause : clauses) {
                if (clause.premise.pixel_index == pixel || clause.conclusion.pixel_index == pixel) {
                    count++;
                }
            }
            pixel_counts.push_back({count, pixel});
        }
    }
    std::sort(pixel_counts.rbegin(), pixel_counts.rend());  // Most constrained first
    
    // Try guessing each undecided pixel that appears in clauses
    bool made_progress;
    int max_iterations = 3;  // Limit iterations to avoid exponential blowup
    int iteration = 0;
    
    do {
        if (check_timeout()) return true;
        
        made_progress = false;
        iteration++;
        
        for (const auto& [count, i] : pixel_counts) {
            if (check_timeout()) return true;
            if (grid_[i] != Pixel::UNDECIDED) continue;
            
            // Save current state
            auto saved_grid = grid_;
            
            // Try setting to WHITE - use lighter-weight check
            grid_[i] = Pixel::WHITE;
            bool white_works = full_settle() && !has_contradiction();
            
            // Restore and try BLACK
            grid_ = saved_grid;
            grid_[i] = Pixel::BLACK;
            bool black_works = full_settle() && !has_contradiction();
            
            // Restore original state
            grid_ = saved_grid;
            
            if (!white_works && !black_works) {
                // Contradiction - puzzle is unsolvable
                return false;
            } else if (!white_works) {
                // Must be black
                grid_[i] = Pixel::BLACK;
                if (!solver0()) return false;
                made_progress = true;
            } else if (!black_works) {
                // Must be white
                grid_[i] = Pixel::WHITE;
                if (!solver0()) return false;
                made_progress = true;
            }
            // else: both work, can't determine yet
            
            if (is_solved() || timed_out_) return true;
        }
        
        // Also try remaining undecided pixels not in clauses
        if (!made_progress && iteration == 1 && !timed_out_) {
            for (int i = 0; i < width_ * height_; ++i) {
                if (check_timeout()) return true;
                if (grid_[i] != Pixel::UNDECIDED) continue;
                if (clause_pixels.count(i) > 0) continue;  // Already tried
                
                auto saved_grid = grid_;
                
                grid_[i] = Pixel::WHITE;
                bool white_works = full_settle() && !has_contradiction();
                
                grid_ = saved_grid;
                grid_[i] = Pixel::BLACK;
                bool black_works = full_settle() && !has_contradiction();
                
                grid_ = saved_grid;
                
                if (!white_works && !black_works) {
                    return false;
                } else if (!white_works) {
                    grid_[i] = Pixel::BLACK;
                    if (!solver0()) return false;
                    made_progress = true;
                } else if (!black_works) {
                    grid_[i] = Pixel::WHITE;
                    if (!solver0()) return false;
                    made_progress = true;
                }
                
                if (is_solved() || timed_out_) return true;
            }
        }
        
    } while (made_progress && iteration < max_iterations && !timed_out_);
    
    return true;
}

bool Solver::complete_solution() {
    // After logical deduction, complete the solution using recursive backtracking
    return complete_solution_recursive(0);
}

bool Solver::complete_solution_recursive(int start_idx) {
    if (check_timeout()) return false;
    
    // Run propagation first
    if (!full_settle()) {
        return false;  // Contradiction
    }
    
    if (timed_out_) return false;
    
    // Find first undecided pixel from start_idx
    int first_unknown = -1;
    for (int i = start_idx; i < width_ * height_; ++i) {
        if (grid_[i] == Pixel::UNDECIDED) {
            first_unknown = i;
            break;
        }
    }
    
    if (first_unknown == -1) {
        return true;  // All solved!
    }
    
    // Save current state
    auto saved_grid = grid_;
    
    // Try BLACK first
    grid_[first_unknown] = Pixel::BLACK;
    if (complete_solution_recursive(first_unknown + 1)) {
        return true;  // Found solution
    }
    
    if (timed_out_) {
        grid_ = saved_grid;
        return false;
    }
    
    // BLACK didn't work, restore and try WHITE
    grid_ = saved_grid;
    grid_[first_unknown] = Pixel::WHITE;
    if (complete_solution_recursive(first_unknown + 1)) {
        return true;  // Found solution
    }
    
    // Neither worked, restore and backtrack
    grid_ = saved_grid;
    return false;
}

SolverResult Solver::solve() {
    start_time_ = std::chrono::steady_clock::now();
    timed_out_ = false;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    SolverResult result;
    result.width = width_;
    result.height = height_;
    result.timed_out = false;
    
    // Helper to finalize result on timeout
    auto finalize_timeout = [&]() {
        result.grid = grid_;
        result.unknowns = count_unknowns();
        result.is_solved = false;
        result.is_unique = false;
        result.timed_out = true;
        result.solver_level = "timeout";
        auto end = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    };
    
    // First try simple solving with FullSettle
    if (!full_settle()) {
        // Contradiction in initial settling
        result.grid = grid_;
        result.unknowns = count_unknowns();
        result.is_solved = false;
        result.is_unique = false;
        result.solver_level = "contradiction";
        auto end = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
    
    if (timed_out_) return finalize_timeout();
    
    if (is_solved()) {
        result.grid = grid_;
        result.unknowns = 0;
        result.is_solved = true;
        result.is_unique = true;  // Simple puzzles are unique
        result.solver_level = "simple";
        auto end = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
    
    // Try Solver0 (FullSettle + 2SAT)
    if (!solver0()) {
        result.grid = grid_;
        result.unknowns = count_unknowns();
        result.is_solved = false;
        result.is_unique = false;
        result.solver_level = "contradiction";
        auto end = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
    
    if (timed_out_) return finalize_timeout();
    
    if (is_solved()) {
        result.grid = grid_;
        result.unknowns = 0;
        result.is_solved = true;
        result.is_unique = true;
        result.solver_level = "0-solvable";
        auto end = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
    
    // Try Solver1 (Solver0 + guessing)
    if (!solver1()) {
        result.grid = grid_;
        result.unknowns = count_unknowns();
        result.is_solved = false;
        result.is_unique = false;
        result.solver_level = "contradiction";
        auto end = std::chrono::high_resolution_clock::now();
        result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return result;
    }
    
    if (timed_out_) return finalize_timeout();
    
    if (is_solved()) {
        result.grid = grid_;
        result.unknowns = 0;
        result.is_solved = true;
        result.is_unique = true;
        result.solver_level = "1-solvable";
    } else {
        // Puzzle has multiple solutions - complete with arbitrary choices
        complete_solution();
        
        if (timed_out_) return finalize_timeout();
        
        result.grid = grid_;
        result.unknowns = count_unknowns();
        result.is_solved = is_solved();
        result.is_unique = false;  // Multiple solutions exist
        result.solver_level = result.is_solved ? "completed" : "partial";
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    result.solve_time_ms = std::chrono::duration<double, std::milli>(end - start).count();
    
    return result;
}

} // namespace nonogram
