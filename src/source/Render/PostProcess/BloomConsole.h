#pragma once

/// Simple console command parser for Bloom effect tuning.
/// Usage:
///   /bloom enable <0|1>       -- Turn bloom on/off
///   /bloom threshold <0..1>   -- Brightness cutoff (default 0.65)
///   /bloom strength <float>   -- Glow intensity (default 1.1)
///   /bloom blur <1..8>        -- Blur iterations (default 4)
///   /bloom status             -- Print current settings
///
/// Example: "/bloom threshold 0.5" or "/bloom strength 1.5"
///
class BloomConsole
{
public:
    /// Parse and execute a bloom command.
    /// Returns true if command was recognized and executed, false otherwise.
    static bool ExecuteCommand(const std::string& command);

private:
    static void PrintStatus();
};
