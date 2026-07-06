#pragma once
// The §5a binary I/O token registry. fs.openRead/openWrite mint a single-use token bound to a
// filesystem path; the payload then rides the app://io scheme handler (a fetch), never the message
// channel. Tokens expire on completion or after 30 s idle (contract §5a, spec/commands/fs.md).
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

struct IoToken {
    std::string id;
    std::string path;
    bool write = false;  // true = openWrite (PUT), false = openRead (GET)
    unsigned long long createdMs = 0;
};

class IoChannel {
public:
    std::string mint(const std::string& path, bool write);
    // Look up and CONSUME the token (single-use, §5a). Empty if unknown, expired, or already used.
    std::optional<IoToken> consume(const std::string& id);

private:
    static constexpr unsigned long long IdleMs = 30000;
    std::mutex mu_;
    std::unordered_map<std::string, IoToken> tokens_;
    void sweep();
};
