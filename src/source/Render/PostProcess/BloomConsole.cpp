#include "StdAfx.h"
#include "BloomConsole.h"
#include "Bloom.h"
#include <sstream>
#include <iomanip>

bool BloomConsole::ExecuteCommand(const std::string& command)
{
    std::istringstream iss(command);
    std::string prefix, subcommand;

    iss >> prefix >> subcommand;

    // Check if command starts with "/bloom"
    if (prefix != "/bloom" && prefix != "bloom")
        return false;

    if (subcommand == "enable" || subcommand == "enabled")
    {
        int value = 0;
        if (iss >> value)
        {
            Bloom::SetEnabled(value != 0);
            g_ErrorReport.Write(L"> Bloom: %s\r\n", Bloom::IsEnabled() ? L"ENABLED" : L"DISABLED");
            return true;
        }
    }
    else if (subcommand == "threshold")
    {
        float value = 0.0f;
        if (iss >> value)
        {
            // Clamp to [0..1]
            value = (value < 0.0f) ? 0.0f : (value > 1.0f) ? 1.0f : value;
            Bloom::SetThreshold(value);
            g_ErrorReport.Write(L"> Bloom threshold: %.3f\r\n", Bloom::GetThreshold());
            return true;
        }
    }
    else if (subcommand == "strength")
    {
        float value = 0.0f;
        if (iss >> value)
        {
            // Allow any positive value
            if (value < 0.0f) value = 0.0f;
            Bloom::SetStrength(value);
            g_ErrorReport.Write(L"> Bloom strength: %.3f\r\n", Bloom::GetStrength());
            return true;
        }
    }
    else if (subcommand == "blur")
    {
        int value = 0;
        if (iss >> value)
        {
            Bloom::SetBlurPasses(value);  // SetBlurPasses clamps to [1..8]
            g_ErrorReport.Write(L"> Bloom blur passes: %d\r\n", Bloom::GetBlurPasses());
            return true;
        }
    }
    else if (subcommand == "status" || subcommand == "info")
    {
        PrintStatus();
        return true;
    }
    else if (subcommand == "help" || subcommand == "?")
    {
        g_ErrorReport.Write(L"Bloom Console Commands:\r\n");
        g_ErrorReport.Write(L"  /bloom enable <0|1>       -- Turn bloom on/off\r\n");
        g_ErrorReport.Write(L"  /bloom threshold <0..1>   -- Brightness cutoff\r\n");
        g_ErrorReport.Write(L"  /bloom strength <float>   -- Glow intensity\r\n");
        g_ErrorReport.Write(L"  /bloom blur <1..8>        -- Blur iterations\r\n");
        g_ErrorReport.Write(L"  /bloom status             -- Show current settings\r\n");
        return true;
    }

    return false;
}

void BloomConsole::PrintStatus()
{
    g_ErrorReport.Write(L"Bloom Effect Status:\r\n");
    g_ErrorReport.Write(L"  Enabled:   %s\r\n", Bloom::IsEnabled() ? L"YES" : L"NO");
    g_ErrorReport.Write(L"  Threshold: %.3f (brightness cutoff)\r\n", Bloom::GetThreshold());
    g_ErrorReport.Write(L"  Strength:  %.3f (glow intensity)\r\n", Bloom::GetStrength());
    g_ErrorReport.Write(L"  Blur:      %d passes\r\n", Bloom::GetBlurPasses());
}
