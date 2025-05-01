#ifndef TOPIC_MATCHER_H
#define TOPIC_MATCHER_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional> // For std::function
#include <utility>    // For std::pair
#include <stdexcept>  // For std::invalid_argument

class TopicMatcher {
public:
    // Type alias for the function signature expected for matching
    using MatchFunction = std::function<bool(const std::string& /* topic */, const std::string& /* pattern */)>;

    // Constructor takes the matching function to use
    explicit TopicMatcher(MatchFunction matcher);

    // Disable copy and assignment
    TopicMatcher(const TopicMatcher&) = delete;
    TopicMatcher& operator=(const TopicMatcher&) = delete;

    /**
     * @brief Adds or updates a subscription for a client to a topic pattern.
     * @param client_id The ID of the subscribing client.
     * @param pattern The topic pattern (can contain wildcards).
     * @param sf_flag The store-and-forward flag for this subscription.
     */
    void add_subscription(const std::string& client_id, const std::string& pattern, bool sf_flag);

    /**
     * @brief Removes a specific subscription for a client.
     * @param client_id The ID of the client.
     * @param pattern The topic pattern to unsubscribe from.
     */
    void remove_subscription(const std::string& client_id, const std::string& pattern);

    /**
     * @brief Removes all subscriptions associated with a client (e.g., on disconnect).
     * @param client_id The ID of the client to remove.
     */
    void remove_client(const std::string& client_id);

    /**
     * @brief Finds all clients whose subscriptions match the given topic.
     * @param topic The specific topic published (no wildcards).
     * @return A map where keys are matching client IDs and values are the
     *         effective store-and-forward flag (true if *any* matching
     *         subscription for that client has SF=1).
     */
    std::map<std::string, bool> find_matches(const std::string& topic) const;


private:
    // Primary structure: Pattern -> (ClientID -> SF_Flag)
    std::map<std::string, std::map<std::string, bool>> pattern_to_clients;

    // Helper structure: ClientID -> {Set of patterns subscribed to}
    std::map<std::string, std::set<std::string>> client_to_patterns;

    // The function used to determine if a topic matches a pattern
    MatchFunction match_func;
};

#endif // TOPIC_MATCHER_H