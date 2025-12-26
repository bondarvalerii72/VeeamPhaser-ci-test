#include "Command.hpp"

class ProcessCarverCommand : public Command {
public:
    int run() override;

private:
    static ProcessCarverCommand instance; // Static instance to trigger registration
    ProcessCarverCommand(bool reg=false);
};
