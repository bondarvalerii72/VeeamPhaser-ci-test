#pragma once
#include "core/CMeta.hpp"

using namespace Veeam::VBK;

struct FileTestInfo {
    // initialized
    const std::string name;
    const std::string pathname;
    const fs::path md_fname;
    const PhysPageId ppi;
    const size_t size;
    const size_t total_blocks;
    Veeam::VBK::EFileType type;

    // calculated
    size_t sparse_blocks = 0;
    size_t nOK = 0;     // OK blocks
    size_t nMissMD = 0; // block metadata completely missing
    size_t nMissHT = 0; // blocks not found in HT, but we know it's hash, size, etc
    size_t nErrDecomp = 0;
    size_t nErrCRC = 0;
    size_t nReadErr = 0;

    FileTestInfo(const CMeta::VFile& vFile, const std::string& pathname, const fs::path& md_fname) :
        name(vFile.name),
        pathname(pathname),
        md_fname(md_fname),
        ppi(vFile.attribs.ppi),
        size(vFile.attribs.filesize),
        total_blocks(vFile.attribs.nBlocks),
        type(vFile.type)
    {}

    std::string header() const;
    double percent() const;

    std::string to_string(bool color=false) const;
    std::string to_json() const;
};
