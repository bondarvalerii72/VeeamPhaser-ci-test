#include "Command.hpp"

class CarveCommand : public Command {
public:
    int run() override;

private:
    static CarveCommand instance; // Static instance to trigger registration
    CarveCommand(bool reg=false);
};
