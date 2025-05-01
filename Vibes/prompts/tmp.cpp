#include <fstream>

std::string FILES[] = {
    "../../"
    "include/"
    "common.h",
    "../../"
    "include/"
    "circular_buffer.h",
    "../../"
    "include/"
    "subscriber.h",
    "../../"
    "include/"
    "server.h",
    "../../"
    "lib/"
    "common.cpp",
    "../../"
    "lib/"
    "circular_buffer.cpp",
    "../../"
    "src/"
    "subscriber.cpp",
    "../../"
    "src/"
    "server.cpp",
    "../../"
    "Makefile",
};

int main()
{
    std::ofstream file("output.txt");
    for (const auto &file_path : FILES)
    {
        std::ifstream input_file(file_path);
        if (input_file.is_open())
        {
            file << "Contents of " << file_path << ":\n";
            file << "```c++\n";
            file << input_file.rdbuf(); // Read the entire file into the output
            file << "```\n";
            file << "\n\n"; // Add some spacing between files
            input_file.close();
        }
        else
        {
            file << "Failed to open " << file_path << "\n";
        }
    }

    file << "in server.cpp the function handle_udp_message loops through two hashmaps: subscribers and sub.topics. Do you think you can keep a hashmap but with the elements reversed so you can reduce the time complexity?\n"
            "Can you make the topicMatches even more efficient? Maybe use a trie? I don't know if it is a good ideea, but make it more efficient.\n"
            "Do not change the the messages of the logs\n"
            "Do not use global variables\n"
            "AND WHATEVER YOU DO DO NOT CHANGE THE FUNCTIONALITY OF THE CODE\n";
    return 0;
}
