#include "nrf_commands.h"
#include "modules/NRF24/nrf_jammer.h"
#include <globals.h>

static uint32_t nrfJamCallback(cmd *c) {
    Command cmd(c);

    String mode = cmd.getArgument("mode").getValue();
    String secondsStr = cmd.getArgument("seconds").getValue();
    String dwellStr = cmd.getArgument("dwell_ms").getValue();
    String strategy = cmd.getArgument("strategy").getValue();
    mode.trim();
    strategy.trim();
    strategy.toLowerCase();

    uint32_t seconds = secondsStr.toInt();
    uint16_t dwellMs = dwellStr.toInt();
    bool flooding = strategy == "flood" || strategy == "flooding" || strategy == "data";

    if (mode.length() == 0) mode = "full";
    if (seconds == 0) seconds = 10;

    return nrf_serial_jam(mode, seconds * 1000UL, dwellMs, flooding);
}

static void addNrfJamArgs(Command &cmd) {
    cmd.addPosArg("mode", "full");
    cmd.addPosArg("seconds", "10");
    cmd.addPosArg("dwell_ms", "0");
    cmd.addPosArg("strategy", "cw");
}

void createNrfCommands(SimpleCLI *cli) {
    Command jam = cli->addCommand("nrfjam,nrf24jam", nrfJamCallback);
    addNrfJamArgs(jam);

    Command nrf = cli->addCompositeCmd("nrf,nrf24");
    Command nrfJam = nrf.addCommand("jam", nrfJamCallback);
    addNrfJamArgs(nrfJam);
}
