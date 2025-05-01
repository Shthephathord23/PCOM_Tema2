#include "topic_matcher.h"
#include <utility> // For std::move

TopicMatcher::TopicMatcher(MatchFunction matcher) : match_func(std::move(matcher)) {
    if (!match_func) {
        throw std::invalid_argument("Matcher function cannot be null");
    }
}

void TopicMatcher::add_subscription(const std::string& client_id, const std::string& pattern, bool sf_flag) {
    // Add to primary map
    pattern_to_clients[pattern][client_id] = sf_flag;
    // Add to helper map
    client_to_patterns[client_id].insert(pattern);
}

void TopicMatcher::remove_subscription(const std::string& client_id, const std::string& pattern) {
    // Remove from primary map
    auto pattern_it = pattern_to_clients.find(pattern);
    if (pattern_it != pattern_to_clients.end()) {
        pattern_it->second.erase(client_id);
        // If no more clients for this pattern, remove the pattern entry entirely
        if (pattern_it->second.empty()) {
            pattern_to_clients.erase(pattern_it);
        }
    }

    // Remove from helper map
    auto client_it = client_to_patterns.find(client_id);
    if (client_it != client_to_patterns.end()) {
        client_it->second.erase(pattern);
        // If client has no more patterns, remove the client entry entirely
        if (client_it->second.empty()) {
            client_to_patterns.erase(client_it);
        }
    }
}

void TopicMatcher::remove_client(const std::string& client_id) {
    auto client_it = client_to_patterns.find(client_id);
    if (client_it != client_to_patterns.end()) {
        // Iterate through a copy of the patterns, as remove_subscription modifies the set
        std::set<std::string> patterns_copy = client_it->second;
        for (const std::string& pattern : patterns_copy) {
            // Use remove_subscription to ensure pattern_to_clients is also cleaned up
            remove_subscription(client_id, pattern);
        }
        // The client entry in client_to_patterns should now be empty and removed
        // by the logic within remove_subscription. Explicitly erase just in case.
        client_to_patterns.erase(client_id);
    }
     // Optional defensive check (can be slow):
     // Iterate through all patterns and remove the client ID
     // for (auto it = pattern_to_clients.begin(); it != pattern_to_clients.end(); /* no increment here */) {
     //     it->second.erase(client_id);
     //     if (it->second.empty()) {
     //         it = pattern_to_clients.erase(it); // Erase empty pattern and advance iterator
     //     } else {
     //         ++it; // Advance iterator
     //     }
     // }
}


// Returns: map<ClientID, effective_sf_flag>
std::map<std::string, bool> TopicMatcher::find_matches(const std::string& topic) const {
    std::map<std::string, bool> results; // ClientID -> effective_sf_flag

    // Iterate through all known patterns
    for (const auto& pattern_pair : pattern_to_clients) {
        const std::string& pattern = pattern_pair.first;
        const auto& clients = pattern_pair.second; // map<ClientID, SF_Flag>

        // Check if the current pattern matches the given topic using the stored function
        if (match_func(topic, pattern)) {
            // If it matches, update the results for all clients subscribed to this pattern
            for (const auto& client_pair : clients) {
                const std::string& client_id = client_pair.first;
                bool sf_flag = client_pair.second;

                // Update the effective SF flag for the client.
                // If the client is already in results, set SF=true if *either* the
                // existing flag OR the current flag is true (effectively ORing the SF flags).
                auto [it, inserted] = results.try_emplace(client_id, sf_flag);
                if (!inserted) { // Client already existed in results
                    it->second = it->second || sf_flag; // Update with OR logic
                }
            }
        }
    }
    return results;
}