#ifndef SERVER_TOPIC_H
#define SERVER_TOPIC_H

#include <string>
#include <vector>
#include <sstream>

// Function to match a topic against a pattern (with wildcards '+' and '*')
// Using the Dynamic Programming approach from the provided refactored code.
bool topicMatches(const std::string& topic, const std::string& pattern);

#endif // SERVER_TOPIC_H