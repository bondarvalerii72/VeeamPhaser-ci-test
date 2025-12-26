#include "Command.hpp"

class MetaProcessCommand : public Command {
public:
    int run() override;

private:
    static MetaProcessCommand instance; // Static instance to trigger registration
    MetaProcessCommand(bool reg=false);
};
