#include "Command.hpp"

class VBKCommand : public Command {
public:
    int run() override;

private:
    static VBKCommand instance; // Static instance to trigger registration
    VBKCommand(bool reg = false);

    friend class VBKCommandTest;
    friend class CmdTestBase<VBKCommand>;
};
