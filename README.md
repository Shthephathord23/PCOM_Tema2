# Documentation: `topicMatches` Algorithm

## Purpose

The `topicMatches` function, located in `src/server.cpp`, is responsible for determining if a given `topic` string matches a `pattern` string, following MQTT-style wildcard conventions.

*   **Topics:** Hierarchical strings separated by `/` (e.g., `sensor/+/reading`, `data/nyc/temp`).
*   **Patterns:** Can contain literal segments, single-level wildcards (`+`), and multi-level wildcards (`*`).
    *   `+`: Matches exactly one segment at that level (e.g., `a/+/c` matches `a/b/c` but not `a/c` or `a/b/d/c`).
    *   `*`: Matches zero or more segments at that level and all subsequent levels (e.g., `a/*` matches `a`, `a/b`, `a/b/c`; `a/*/d` matches `a/d`, `a/b/d`, `a/b/c/d`).

## Motivation for Dynamic Programming

An earlier implementation might have used simple recursion. However, recursive approaches for this type of pattern matching, especially with the multi-level wildcard (`*`), can lead to:

1.  **Redundant Computations:** The same subproblems (e.g., matching the tail end of a topic against the tail end of a pattern) might be computed multiple times.
2.  **Potential Exponential Time Complexity:** In worst-case scenarios involving multiple `*` wildcards, the number of recursive calls could grow exponentially.
3.  **String Manipulation Overhead:** Recursion often involves creating substrings, which can be inefficient.

To overcome these issues and provide a guaranteed polynomial time solution, a **Dynamic Programming (DP)** approach was implemented.

## Dynamic Programming Approach

The core idea is to build up a solution by determining matches for increasingly larger prefixes of the topic and pattern.

1.  **Segmentation:** Both the `topic` and `pattern` strings are first split into segments using `/` as the delimiter. Let `t_segs` be the topic segments (length N) and `p_segs` be the pattern segments (length M).

2.  **DP State:** We define `dp[i][j]` as a boolean value indicating whether the first `i` segments of the topic (`t_segs[0...i-1]`) match the first `j` segments of the pattern (`p_segs[0...j-1]`).

3.  **Base Cases:**
    *   `dp[0][0] = true`: An empty topic matches an empty pattern.
    *   `dp[i][0] = false` for `i > 0`: A non-empty topic cannot match an empty pattern.
    *   `dp[0][j]` for `j > 0`: An empty topic can only match a pattern prefix if that prefix consists solely of `*` segments (since `*` can match zero segments). So, `dp[0][j] = true` if `p_segs[j-1] == '*'` AND `dp[0][j-1]` was true.

4.  **Recurrence Relations (Transitions):** We calculate `dp[i][j]` for `i > 0` and `j > 0` based on the type of the `j`-th pattern segment (`p_segs[j-1]`) and the `i`-th topic segment (`t_segs[i-1]`):

    *   **If `p_segs[j-1]` is `+`:**
        The `+` must match the current topic segment `t_segs[i-1]`. The overall match depends on whether the prefixes *before* these segments matched.
        `dp[i][j] = dp[i-1][j-1]`

    *   **If `p_segs[j-1]` is `*`:**
        The `*` wildcard offers two possibilities:
        a.  It matches *zero* segments at the current level. The match then depends on whether the pattern *up to `j-1`* (excluding the current `*`) matched the current topic prefix (`t_segs[0...i-1]`). This corresponds to `dp[i][j-1]`.
        b.  It matches *one or more* segments, including the current topic segment `t_segs[i-1]`. The match then depends on whether the pattern *up to `j`* (including the current `*`) matched the *previous* topic prefix (`t_segs[0...i-2]`). This corresponds to `dp[i-1][j]`.
        Since either case results in a match:
        `dp[i][j] = dp[i][j-1] || dp[i-1][j]`

    *   **If `p_segs[j-1]` is a Literal Segment:**
        For a match to occur, the current pattern segment must exactly match the current topic segment (`p_segs[j-1] == t_segs[i-1]`), AND the prefixes before these segments must have matched.
        `dp[i][j] = (p_segs[j-1] == t_segs[i-1]) && dp[i-1][j-1]`

5.  **Final Result:** The overall match between the full topic and the full pattern is given by `dp[N][M]`.

## Space Optimization (Rolling Array)

A full 2D DP table would require O(N * M) space. However, notice that calculating the values for row `i` only requires values from row `i-1` and the current row `i`. We don't need rows `i-2`, `i-3`, etc.

This allows for a space optimization using the **Rolling Array Technique**:

*   Only two rows (vectors) of size `M+1` are maintained: `prev_dp` (representing row `i-1`) and `curr_dp` (representing row `i`).
*   When calculating `curr_dp[j]`, we use values from `prev_dp` and potentially `curr_dp[j-1]` (which has already been calculated in the current iteration).
*   After row `i` is fully computed (all `j` values for `curr_dp`), `prev_dp` is updated to become `curr_dp` before proceeding to the next row (`i+1`).

This reduces the space complexity significantly.

## Complexity

*   **Time Complexity:** O(N * M), where N is the number of topic segments and M is the number of pattern segments. The initial string splitting takes O(T + P) time (T=topic length, P=pattern length), but the dominant factor is filling the DP table/rows.
*   **Space Complexity:** O(M), where M is the number of pattern segments, due to the rolling array optimization.

## Benefits

*   **Correctness:** Accurately implements the specified wildcard matching logic.
*   **Efficiency:** Guarantees a polynomial time complexity, avoiding the potential exponential blowup of naive recursion.
*   **Space Savings:** The rolling array optimization makes the algorithm practical even for topics/patterns with a moderate number of segments.