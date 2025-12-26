#pragma once
#include "utils/common.hpp"

#define REGISTER_COMMAND(klass)                 \
    klass klass::instance(true);                \
    extern "C" void force_link_##klass() {}     // Define function to force linker to keep TU

// for declaring friends like "friend class CmdTestBase<VBKCommand>"
template<typename T> class CmdTestBase;

class Command {
    public:
        virtual int run() = 0;
        virtual ~Command() {}

        static std::map<std::string, Command*>& registry() {
            static std::map<std::string, Command*> registry;
            return registry;
        }

        argparse::ArgumentParser& parser() {
            return m_parser;
        }

    protected:
        Command(bool reg, const char* name, const char* description) : m_parser(name, "", argparse::default_arguments::help) {
            m_parser.add_description(description);
            register_common_args(m_parser);
            if( reg ){
                if( registry().find(name) != registry().end() ){
                    throw std::runtime_error("Command already registered: " + std::string(name));
                }
                registry()[name] = this;
            }
        }

        argparse::ArgumentParser m_parser;
};
