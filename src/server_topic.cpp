#include "server_topic.h"
#include <vector>
#include <string>
#include <sstream>
#include <algorithm> // For std::fill if needed

// Implementation uses Dynamic Programming approach
bool topicMatches(const std::string& topic, const std::string& pattern) {
    // 1. Split Topic and Pattern into Segments
    std::vector<std::string> t_segs, p_segs;
    std::string segment;
    std::stringstream ss_t(topic);
    while (getline(ss_t, segment, '/')) {
        t_segs.push_back(segment);
    }
    std::stringstream ss_p(pattern);
    while (getline(ss_p, segment, '/')) {
        p_segs.push_back(segment);
    }

    size_t N = t_segs.size();
    size_t M = p_segs.size();

    // 2. Create DP rows (size M+1 for base cases)
    std::vector<bool> prev_dp(M + 1, false);
    std::vector<bool> curr_dp(M + 1, false);

    // 3. Base Case: Initialize prev_dp (representing dp[0][j])
    prev_dp[0] = true; // Empty pattern matches empty topic
    for (size_t j = 1; j <= M; ++j) {
        if (p_segs[j - 1] == "*") {
            prev_dp[j] = prev_dp[j - 1]; // '*' can match zero segments at the beginning
        }
        // else prev_dp[j] remains false
    }

    // 4. Fill the "Table" Row by Row (Iterating through topic segments)
    for (size_t i = 1; i <= N; ++i) {
        // Base case for the current row: dp[i][0] is always false for i > 0 (non-empty topic cannot match empty pattern)
        curr_dp[0] = false;
        for (size_t j = 1; j <= M; ++j) {
            const std::string& p_seg = p_segs[j - 1];
            const std::string& t_seg = t_segs[i - 1];

            if (p_seg == "+") {
                // '+' matches current topic segment iff previous pattern matched previous topic segment
                curr_dp[j] = prev_dp[j - 1];
            } else if (p_seg == "*") {
                // '*' matches if:
                // a) '*' matches zero segments (curr_dp[j-1] is true -> previous pattern matched current topic)
                // b) '*' matches the current segment t_seg (prev_dp[j] is true -> current pattern '*' matches previous topic)
                curr_dp[j] = curr_dp[j - 1] || prev_dp[j];
            } else {
                // Literal match required
                curr_dp[j] = (p_seg == t_seg) && prev_dp[j - 1];
            }
        }
        // Update prev_dp for the next iteration
        prev_dp = curr_dp;
        // No need to clear curr_dp explicitly as it gets overwritten
    }

    // 5. Result is the last element of the last computed row (stored in prev_dp)
    return prev_dp[M];
}