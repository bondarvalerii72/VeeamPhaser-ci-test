#include "Command.hpp"

class Scan2Command : public Command {
public:
    int run() override;

private:
    static Scan2Command instance; // Static instance to trigger registration
    Scan2Command(bool reg=false);

    friend class MDCommandTest;
    friend class Scan2CommandTest;
};
