#include "Command.hpp"

class TOCCommand : public Command {
public:
    int run() override;

private:
    static TOCCommand instance; // Static instance to trigger registration
    TOCCommand(bool reg=false);

    void process_toc(std::string fname);
    std::string veAcquirePwd();
};
