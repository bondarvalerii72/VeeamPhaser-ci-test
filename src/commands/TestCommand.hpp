#include "Command.hpp"

#define TEST_CMD_NAME "test"

class TestCommand : public Command {
public:
    int run() override;

private:
    static TestCommand instance; // Static instance to trigger registration
    TestCommand(bool reg=false);
};
