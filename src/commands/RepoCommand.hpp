#ifndef REPOMMAND_HPP
#define REPOMMAND_HPP

#include "Command.hpp"

class RepoCommand : public Command {
public:
    int run() override;

private:
    static RepoCommand instance;
    RepoCommand(bool reg = false);
};

#endif