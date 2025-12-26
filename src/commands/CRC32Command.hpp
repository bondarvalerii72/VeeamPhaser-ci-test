#include "Command.hpp"

class CRC32Command : public Command {
public:
    int run() override;

private:
    static CRC32Command instance; // Static instance to trigger registration
    CRC32Command(bool reg=false);
};
