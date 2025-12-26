#include "Command.hpp"

class BlocksCommand : public Command {
public:
    int run() override;

private:
    static BlocksCommand instance; // Static instance to trigger registration
    BlocksCommand(bool reg=false);
};
