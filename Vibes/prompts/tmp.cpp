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

    file << "make a documentation of the code in README.md format. Be verbose\n";
    return 0;
}
