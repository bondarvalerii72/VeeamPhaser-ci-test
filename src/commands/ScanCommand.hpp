#include "Command.hpp"

class ScanCommand : public Command {
public:
    int run() override;

private:
    static ScanCommand instance; // Static instance to trigger registration
    ScanCommand(bool reg=false);
};
