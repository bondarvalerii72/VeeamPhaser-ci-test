/**
 * @file Scanner.cpp
 * @brief Implementation of the legacy Scanner for metadata block detection (V1).
 *
 * This file provides the original scanning implementation for detecting metadata
 * blocks, slots, and banks in VIB/VBK files. It performs signature-based scanning,
 * validates structures, merges banks from multiple slots, and outputs reconstructed
 * metadata. This is the legacy version kept for compatibility; new code should use
 * ScannerV2.
 */

#include "Scanner.hpp"
#include "core/CMeta.hpp"
#include "utils/common.hpp"
#include "utils/Progress.hpp"
#include "io/Reader.hpp"
#include "Veeam/VBK.hpp"

#include <fstream>
#include <unordered_set>

#include <zlib.h>

using namespace Veeam::VBK;

/**
 * @brief Guesses the metadata ID by analyzing page headers in a metadata buffer.
 *
 * Examines multiple pages to find consistent metadata ID values and returns
 * the average. Used when metadata ID cannot be determined by other means.
 *
 * @param vm Buffer containing metadata pages.
 * @param[out] mpidOut Guessed metadata ID.
 * @return Always true (legacy behavior).
 */
bool vGuessMetaID(buf_t& vm, uint32_t& mpidOut) {
//    bool result = false;
    uint64_t sum = 0;
    uint32_t cnt = 0;
    uint32_t numBlocks = static_cast<uint32_t>(vm.size() / PAGE_SIZE)-1;

    for (uint32_t i = 0; i < numBlocks; ++i) {
        size_t midIndex = i * PAGE_SIZE + 0x1008;
        size_t mpidIndex = i * PAGE_SIZE + 0x100C;

        //simple check to prevent oob
        if (midIndex + sizeof(uint32_t) > vm.size() || mpidIndex + sizeof(uint32_t) > vm.size()) {
            throw std::out_of_range("vGuessMetaID : index out of bounds while accessing vm.");
        }

        uint32_t bank_id = *(uint32_t*) &vm[midIndex];
        uint32_t mpid = *(uint32_t*) &vm[mpidIndex];

        if (bank_id == i) {
            if ((mpid & 0xFFFFF000) == 0) {
                cnt++;
                sum += mpid;
            }
        }
    }

    if (cnt > 1) {
        mpidOut = sum / cnt;
        logger->trace("vGuessMetaID: {:x}", mpidOut);
//        result = true;
    }

    //return result;
    return true; // XXX original function always returns true :(
}

bool vObtainMetaID(buf_t& meta, uint32_t& metaID) {
    bool result = false;
    std::array<uint32_t, 16> id;
    std::vector<uint32_t> cnts;
    uint32_t metaIdCnt = 0;

    //simple check to prevent oob (17 is not a typo)
    if (meta.size() < 17 * PAGE_SIZE + 2*sizeof(uint32_t)) {
        logger->error("vObtainMetaID: meta too small (0x{:x} bytes)", meta.size());
        //throw std::out_of_range("vObtainMetaID : index out of bounds while accessing meta."); 
        return false;
    }
    
    //initialize id with pointers to positions in meta
    for (size_t i = 0; i < id.size(); ++i) {
        id[i] = *(uint32_t*) &meta[(i+1) * PAGE_SIZE + sizeof(uint32_t)];
    }

    //identify the most frequent metaId by counting occurrences of each id value
    for (const uint32_t idval : id) {
        if ((idval & 0xFFFFF000) == 0) {
            if (idval == 0) {
                cnts.resize(1);
            }
            if (cnts.size() <= idval) {
                cnts.resize(idval + 1);
            }
            cnts[idval] = 0;
        }
    }

    for (const uint32_t idval : id) {
        if ((idval & 0xFFFFF000) == 0) {
            cnts[idval]++;
        }
    }

    metaID = 0;
    metaIdCnt = 0;
    
    if (!cnts.empty()) {
        for (size_t i = 0; i < cnts.size(); ++i) {
            if (metaIdCnt < cnts[i]) {
                metaIdCnt = cnts[i];
                metaID = static_cast<uint32_t>(i);
                result = true;
            }
        }
    }

    if (!result) {
        result = vGuessMetaID(meta, metaID);
    } else {
        logger->trace("vObtainMetaID: {:x}", metaID);
        //result = result && vGuessMetaID(meta, metaID); // XXX same as in original code x_x
    }

    return result;
}

bool vGuessMetaIsVIB(buf_t& vm, bool& isVib, uint64_t& blLow, uint64_t& blLow2, uint64_t& blHigh, uint64_t& blHigh2){
    bool result = false;
    size_t mCnt = vm.size() / PAGE_SIZE - 1;

    isVib = false;
    blLow = 0;
    blLow2 = 0;
    blHigh = 0;
    blHigh2 = 0;

    for( size_t i=0; i<mCnt; i++ ){
        VBlockDesc *vBl = (VBlockDesc*)&vm[(i+1)*PAGE_SIZE];
        size_t cnt = 0;

        if( vBl->size == BLOCK_SIZE ){
            while( vBl->size == BLOCK_SIZE && cnt < PAGE_SIZE ){
                if( vBl->size == BLOCK_SIZE && vBl->vib_offset > 0 && vBl->type == 0 ){
                    isVib = true;
                }
                if( blLow == 0 && vBl->id != 0 && vBl->type == 0 ){
                    blLow = vBl->id;
                }
                if( vBl->id != 0 && vBl->type == 0 ){
                    blHigh = vBl->id;
                }
                vBl++;
                cnt += sizeof(VBlockDesc);
            }
        } else {
            BlockDescriptor *vBl2 = (BlockDescriptor*)&vm[(i+1)*PAGE_SIZE];
            if( vBl2->valid() && vBl2->dedup == 1 ){
                while( cnt < PAGE_SIZE ){
                    if( blLow2 == 0 && vBl2->allocSize != 0 ){
                        blLow2 = vBl2->offset;
                    }
                    if( vBl2->allocSize != 0 ){
                        blHigh2 = vBl2->offset;
                    }
                    vBl2++;
                    cnt += sizeof(BlockDescriptor);
                }
            }
        }
    }

    return result;
}

bool vReconstructMDTable(uint32_t mPtr, RecoBlocks& toReconstruct, VMeta& metas, EmptyHashes& emptyBl,
                         uint64_t& totalBl, FileDescriptors& vFiles, uint32_t& remainCnt, BlockDescriptors& bds) {
    static const buf_t emptyMD(PAGE_SIZE);
    std::vector<uint8_t> bitMap;

    uint64_t lastKnBlock = 0;
    uint32_t ex = 0;
    uint32_t bitPos = 0;
    uint32_t bitCnt = 2;
    uint8_t bitCurr = 0;
    uint32_t prevEmptyCnt = 0;
    bool NewFile = (vFiles.size() == 0);
//    bool FetchArrMap = false;
    std::vector<uint8_t> arrTmp;
    bool NewFileApplied = false;

    totalBl = 0;
    emptyBl.clear();
    uint32_t bank_id = toReconstruct[mPtr].bank_id;
    uint32_t metasPtr = toReconstruct[mPtr].metaPtr;

    RecoSupp recoSupport(1); // always 1 element?
    uint32_t tr = 0;
    recoSupport[tr].skip = false;
    recoSupport[tr].bank_id = bank_id;

    for (uint32_t n = 0; n < toReconstruct.size(); ++n) {
        if (metas[toReconstruct[n].metaPtr].size() == 0) {
            logger->debug("Actual Reconstruct Code here");
        } else {
            logger->info("[MultiMD] Start RemainCnt {}",remainCnt);
            if (toReconstruct[n].bank_id == 1)
                // 1 is always unneeded block
                continue;
            if (toReconstruct[n].recoNeeded)
                continue;

            bank_id = toReconstruct[n].bank_id;
            metasPtr = toReconstruct[n].metaPtr;

            for (uint32_t page_id = 1; page_id < metas[bank_id].size() / PAGE_SIZE - 1; ++page_id) {
                if (NewFile && !NewFileApplied) {
                    vFiles.resize(vFiles.size() + 1);
                    NewFileApplied = true;
                }
                if (vFiles[0].parsed.size() < bank_id + 1)
                    vFiles[0].parsed.resize(bank_id + 1);
                if (vFiles[0].parsed[bank_id].size() < page_id + 1)
                    vFiles[0].parsed[bank_id].resize(page_id + 1);

                memcpy(&vFiles[0].parsed[bank_id][page_id], &metas[metasPtr][page_id * PAGE_SIZE], PAGE_SIZE);

                if (memcmp(&metas[metasPtr][page_id * PAGE_SIZE], emptyMD.data(), emptyMD.size()) == 0) {
                    logger->debug("MD[{:x},{:x}] Empty",bank_id,page_id);
                    continue;
                }

                VBlockDesc* vBl = (VBlockDesc*) &metas[metasPtr][page_id * PAGE_SIZE];
                if (vBl->size == BLOCK_SIZE) {
                    logger->info("MD[{:x},{:x}] BlockConst",bank_id,page_id);
                    uint32_t cnt = 0;
                    while (cnt < PAGE_SIZE) {
                        vBl = (VBlockDesc*) &metas[metasPtr][page_id * PAGE_SIZE + cnt];
                        if (vBl->size == BLOCK_SIZE) {
                            totalBl++;
//                            vFiles.back().vBls.push_back(*vBl);
                            uint8_t bitVal = 0;
                            if (vBl->hash == EMPTY_BLOCK_DIGEST || vBl->hash == ZERO_BLOCK_DIGEST) {
                                lastKnBlock++;
                                emptyBl.emplace_back(EmptyHash{{page_id, bank_id}, cnt, lastKnBlock * BLOCK_SIZE});
                                ex++;
                                prevEmptyCnt++;
                            } else {
                                if (NewFile) {
                                    vFiles.back().startPPI.bank_id = bank_id;
                                    vFiles.back().startPPI.page_id = page_id;
                                    NewFile = false;
                                }
                                if ((lastKnBlock - prevEmptyCnt + 1) != vBl->id) {
                                    logger->warn("Block seq. incr. problem: {},{}:{:03x}",bank_id,page_id-1,cnt);
                                }
                                prevEmptyCnt = 0;
                                lastKnBlock = vBl->id;
                                bitVal = 1;
                            }
                            bitCurr |= (bitVal << bitCnt);
                            bitCnt++;
                            if (bitCnt == 8) {
                                bitCnt = 0;
                                bitMap.push_back(bitCurr);
                                bitPos++;
                                bitCurr = 0;
                            }
                        }
                        cnt += sizeof(VBlockDesc);
                    }
                    continue;
                }

                BlockDescriptor* bd = (BlockDescriptor*) &metas[metasPtr][page_id * PAGE_SIZE];
                if ( bd->valid() && bd->dedup == 1 ){
                    logger->info("MD[{:x},{:x}] HT Block",bank_id,page_id);
                    uint32_t cnt = 0;
                    while (cnt < PAGE_SIZE) {
                        bd = reinterpret_cast<BlockDescriptor*>(&metas[metasPtr][page_id * PAGE_SIZE + cnt]);
                        if ( bd->valid() && bd->dedup == 1 ){
                            if (bd->digest != EMPTY_BLOCK_DIGEST && bd->digest != ZERO_BLOCK_DIGEST) {
                                bds[bd->digest] = *bd;
                            } 
                            // Empty block... no need to add it to bd table - results will be unforeseen
                        }
                        cnt += sizeof(BlockDescriptor);
                    }
                    continue;
                }

                // XXX original code first compares dw2 with bank_id, but then assigns dw2 to page_id. mistake?
                MidPid *ps = (MidPid*) &metas[metasPtr][page_id * PAGE_SIZE + 0x8];
                if (ps->page_id == bank_id && ps->bank_id == (page_id - 1)) {
                    std::string sz;
                    char buf[0x100];
                    while (ps->bank_id != 0xFFFFFFFF && ps->page_id != 0xFFFFFFFF) {
//                        recoSupport[tr].recoEntries.emplace_back(RecoEntry{bank_id, page_id - 1, ps->bank_id, ps->page_id});
                        snprintf(buf, sizeof(buf), "%s(%x,%x)", sz.empty() ? "" : "->", ps->bank_id, ps->page_id);
                        sz += buf;
                        if( ps->bank_id == 0 && ps->page_id == 0 ) // not sure if this break is correct, but chain ending with a number if (0,0) does not LGTM // Zed
                            break;
                        ps++;
                    }
                    logger->info("MD[{:x},{:x}] PTH {}",bank_id, page_id, sz);
                    continue;
                }

                if (memcmp(&metas[metasPtr][page_id * PAGE_SIZE + 0x14], "DefinedBlocksMask", 0x11) == 0) {
                    logger->info("MD[{:x},{:x}] DefinedBlocksMask",bank_id,page_id);
                    uint32_t compSize = *(uint32_t*) &metas[metasPtr][page_id * PAGE_SIZE + 0x31]; // compressed size
                    uint32_t srcSize = *(uint32_t*) &metas[metasPtr][page_id * PAGE_SIZE + 0x29]; // uncompressed size
                    NewFile = true;
                    vFiles.back().stopPPI.bank_id = bank_id;
                    vFiles.back().stopPPI.page_id = page_id;
                    vFiles.back().compBitmap.resize(compSize);
                    vFiles.back().compSize = compSize;
                    vFiles.back().srcSize = srcSize;
                    if (compSize > PAGE_SIZE) {
                        uint32_t cnt = compSize; // overall bitmap size (decrementing till 0)
                        uint32_t cnt2 = 0; // this step bitmap size to move (start with 1000-$39 (hdr), then FF4 ..... then Size mod $1000 - $c
                        uint32_t off = 0;  // Copy first chunk

                        uint32_t cxCnt = compSize / PAGE_SIZE;
                        if (compSize % PAGE_SIZE > 0){
                            cxCnt++;
                        }
                        for (uint32_t cx = 0; cx < cxCnt; ++cx) {
                            uint32_t cnt1 = (page_id + cx) * PAGE_SIZE; // offset @ source meta, eg. source data
                            if (cx == 0)
                                cnt1 += 0x39;
                            else
                                cnt1 += 0xC;
                            if (cnt < (PAGE_SIZE - 0xC)) {
                                cnt2 = cnt;
                            } else {
                                if (cx == 0) {
                                    cnt2 = PAGE_SIZE-0x39;
                                } else {
                                    cnt2 = PAGE_SIZE-0xC;
                                }
                            }
                            memcpy(&vFiles.back().compBitmap[off], &metas[metasPtr][cnt1], cnt2);
                            cnt -= cnt2;
                            off += cnt2;
                        }
                    } else {
                        memcpy(&vFiles.back().compBitmap[0], &metas[metasPtr][page_id * PAGE_SIZE + 0x39], compSize);
                    }
                    if (bitCnt > 0 && bitCnt < 8) {
                        bitCnt = 0;
                        bitMap.push_back(bitCurr);
                        bitPos++;
                        bitCurr = 0;
                    }

                    vFiles.back().genBitmap.resize(bitMap.size());
                    memcpy(vFiles.back().genBitmap.data(),bitMap.data(),bitMap.size());
                    bitMap.clear();
                    bitPos = 0;
//                    FetchArrMap = true;
                    NewFileApplied = false;
                    memcpy(&vFiles.back().arrMap[0], &arrTmp[0],arrTmp.size());
                    arrTmp.clear();
                    vFiles.back().unCompBitmap.resize(srcSize);
                    continue;
                }

                uint64_t* qw1 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + 0x8];
                uint64_t* qw2 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + 0x20];
                uint64_t* qw3 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + 0x38];
                if (*qw1 == BLOCK_SIZE && *qw2 == BLOCK_SIZE && *qw3 == BLOCK_SIZE) {
                    logger->info("MD[{:x},{:x}] ArrMap found",bank_id,page_id);
                    qw1 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE];
                    qw2 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + 8];
                    qw3 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + 0x10];
                    uint32_t x = 0;
                    while (*qw1 != 0 && *qw2 != 0 && x + 0x18 <= PAGE_SIZE) {
                        arrTmp.insert(arrTmp.end(), (uint8_t*)qw1, ((uint8_t*)qw1) + 0x18);

                        x += 0x18;
                        qw1 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + x];
                        qw2 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + x + 8];
                        qw3 = (uint64_t*)&metas[metasPtr][page_id * PAGE_SIZE + x + 0x10];
                    }
//                    FetchArrMap = false;
                    continue;
                }
                logger->warn("MD[{:x},{:x}] UNKNOWN STRUCTURE",bank_id,page_id);
            }
            remainCnt--;
        }
    }
    return true;
}


bool vDecompressAllBitmaps(std::vector<FileDescriptor>& vFiles) {
    for (size_t i = 0; i < vFiles.size(); ++i) {
        if (!vFiles[i].compBitmap.empty()) {
            logger->trace("CompBitmap[{}]: {}", i, to_hexdump(vFiles[i].compBitmap));

            if (vFiles[i].compBitmap.size() < 2 || vFiles[i].compSize < 2) {
                throw std::runtime_error("compSize too small");
            }

            uLongf destLen = static_cast<uLongf>(vFiles[i].srcSize);
            vFiles[i].unCompBitmap.resize(destLen);
            int res = uncompress(vFiles[i].unCompBitmap.data(), &destLen, &vFiles[i].compBitmap[0], vFiles[i].compSize);
            if (res == Z_OK) {
                logger->debug("UnCompBitmap[{}]: {}", i, to_hexdump(vFiles[i].unCompBitmap));
                if (destLen != vFiles[i].srcSize) {
                    logger->error("UncompSize mismatch: {} != {}", destLen, vFiles[i].srcSize);
                }
                if (vFiles[i].unCompBitmap.size() < 20) { 
                    throw std::runtime_error("unCompBitmap size too small");
                }
                vFiles[i].diskSize = *(uint64_t*) &vFiles[i].unCompBitmap[0];
                vFiles[i].clustSize = *(uint32_t*) &vFiles[i].unCompBitmap[8];
                vFiles[i].totalBlSize = *(uint64_t*) &vFiles[i].unCompBitmap[12];
                vFiles[i].unCompBitmap.erase(vFiles[i].unCompBitmap.begin(), vFiles[i].unCompBitmap.begin() + 20);
            } else {
                logger->error("uncompress() failed with code {}", res);
            }
        }
    }
    return true;
}

bool check_hbuf(const char* hBuf){
    uint16_t castW = *(uint16_t*)hBuf;
    if( castW <= 1 || castW > 0xfffd ){
        return false;
    }

    if( 0xfefe & (*(uint16_t*) &hBuf[2]) ){
        return false;
    }

    if( 0xfefefefe & (*(uint32_t*) &hBuf[4]) ){
        return false;
    }

    uint64_t *p = (uint64_t*) hBuf;
    for(size_t i=1; i<PAGE_SIZE/sizeof(uint64_t); i++){
        if( 0xfefefefefefefefe & p[i] ){
            return false;
        }
    }

    return true;
}

void saveFile(const buf_t& fm, uint64_t hPos, const std::string in_fname, const std::string ext){
    std::filesystem::path out_fname = get_out_pathname(in_fname, fmt::format("{:012x}{}", hPos, ext));
    logger->trace("saving {}", out_fname);

    std::ofstream of(out_fname, std::ios::binary);
    if( !of.is_open() ){
        throw std::runtime_error("failed to open " + out_fname.string());
    }
    of.write((const char*)fm.data(), fm.size());
}

void Scanner::scan(){
    static const char empty_md_chunk[PAGE_SIZE] = {0};

    Reader reader(m_fname);
    m_fsize = reader.size();
    Progress progress(m_fsize, m_start_offset);

    logger->info("file size: {:x} ({}){}", m_fsize, bytes2human(m_fsize), reader.get_align() ? fmt::format(", align: 0x{:x}", reader.get_align()) : "");
    if( m_fsize <= 0 ){
        logger->critical("Invalid file size: {}", m_fsize);
        return;
    }

    VMeta fMetas;
    std::unordered_set<uint32_t> unneededMetas;
    std::unordered_set<off_t> visited_offsets;
    char fBuf[1024*1024];
    uint32_t firstMetaSize = 0;
    buf_t fm;
    bool cncl = false;
    bool found = false;
    uint32_t obtBankId = 0;
    off_t end_pos = m_fsize - sizeof(fBuf) + 1;
    if( end_pos < 0 ){
        end_pos = m_fsize;
    }

    for( off_t filepos = m_start_offset; filepos < end_pos; filepos += sizeof(fBuf) ){
        progress.update(filepos);

        ssize_t nread = reader.read_at(filepos, fBuf, sizeof(fBuf));

        if( nread != sizeof(fBuf) && filepos + nread < end_pos ){
            logger->critical("read error at {:012x}: nread={:x}, sizeof(fBuf)={:x}", filepos, nread, sizeof(fBuf));
            return;
        }

//        char* p = fBuf-1;
//        while( (p = (char*)memmem(p+1, nread - ((char*)p - fBuf), &EMPTY_BLOCK_DIGEST, sizeof(EMPTY_BLOCK_DIGEST))) ){
        
        // TODO: check not only first found entry?
        if( void* p = memmem(fBuf, nread, &EMPTY_BLOCK_DIGEST, sizeof(EMPTY_BLOCK_DIGEST)) ){
            int64_t revHPos = 0;
            int64_t hPos = filepos + (char*)p - fBuf;

            logger->trace("{:012x}: empty block hash, firstMetaSize = {:x}", hPos, firstMetaSize);

            char hBuf[PAGE_SIZE];
            int64_t inHPos = hPos;
            cncl = false;
            found = false;

            while( hPos > (inHPos-MAX_BANK_SIZE*32) ){ // should be signed comparison
                if( hPos == 0 ){
                    break;
                }
                if( (hPos & 0xfff) == 0 ){
                    hPos -= PAGE_SIZE;
                } else {
                    hPos--;
                }
                if( hPos > (int64_t)(m_fsize - sizeof(hBuf)) ){
                    hPos = m_fsize - 32*MAX_BANK_SIZE;
                    if( hPos < 0 ){
                        hPos = 0;
                    }
                }
                reader.read_at(hPos, hBuf, sizeof(hBuf));
                uint16_t castW = *(uint16_t*)hBuf;
                char prehBuf = hBuf[2];
                bool found = false;
                bool visited = visited_offsets.find(hPos) != visited_offsets.end();
                if( !visited && check_hbuf(hBuf) ){
                    uint32_t currMetaSize = (castW+2)*PAGE_SIZE;
//                    printf("[.] %012jx : currMetaSize %x\n", hPos, currMetaSize);

                    if( firstMetaSize == 0 ){
                        firstMetaSize = currMetaSize;
                    } else {
                        firstMetaSize += currMetaSize;
                    }

                    fm.resize(currMetaSize);
                    if( currMetaSize == 0 ){
                        throw std::runtime_error("currMetaSize is 0");
                    }
                    reader.read_at(hPos, (char*)fm.data(), fm.size());

                    bool vObtRes;
                    if( prehBuf == 0 ){
                        vObtRes = vObtainMetaID(fm, obtBankId);
                    } else {
                        vObtRes = true;
                        obtBankId++;
                    }

                    if( vObtRes ){ // always true in original
                        visited_offsets.insert(hPos);
                        bool lzAvail = false;
                        if( prehBuf == 0 ){
                            bool isNew = (fMetas.size() < obtBankId+1) || fMetas[obtBankId].empty();
                            for( size_t i=0; i<fm.size()/PAGE_SIZE; i++ ){
                                if( *(uint32_t*) &fm[i*PAGE_SIZE] == LZ_START_MAGIC ){
                                    lzAvail = true;
                                    break;
                                }
                            }
                            if( lzAvail ){
                                hPos += fm.size() + 1;
                                continue;
                            }

                            if( isNew ){
                                if( memcmp(&fm[0x1002], &empty_md_chunk[2], sizeof(empty_md_chunk)-2) != 0 ){
                                    // BlockWrite(f3, fm[0], Length(fm));
                                    saveFile(fm, hPos, m_fname, ".bank");
                                    if( fMetas.size() < obtBankId+1 ){
                                        fMetas.resize(obtBankId+1);
                                    }
                                    fMetas[obtBankId] = fm;
                                    logger->info("Found meta ID {:x} entry at {:08x}", obtBankId, hPos);
                                    found = true;
                                }
                            } else {
                                // fm holds mirror of actual obtained Meta
                                // TODO: add procedure of comparing content of
                                // actual meta, and mirror meta and merge two of them to get one functional Meta
                                if( fm == fMetas[obtBankId] ){
                                    logger->info("meta ID {:x} mirror at {:08x} - is coherent with already obtained Meta content", obtBankId, hPos);
                                } else {
                                    logger->warn("meta ID {:x} mirror at {:08x} - is not coherent, need repair and merge meta tables", obtBankId, hPos);
                                }
                                // BlockWrite(f3, fm[0], Length(fm));
                                saveFile(fm, hPos, m_fname, ".bank");
                                found = true;
                            }
                          
                        } else { // prehBuf != 0
                            logger->info("Found meta ID {:x} entry at {:08x}. Meta DISCARDED, Reason: Unneeded MD", obtBankId, hPos);
                            saveFile(fm, hPos, m_fname, ".bank");
                            unneededMetas.insert(obtBankId);
                            found = true;
                        }

                        if( found ){
                            progress.found();
                            if( revHPos == 0 ){
                                revHPos = hPos;
                            }
                            found = false;
                            cncl = true;
                            reader.read_at(hPos + currMetaSize, hBuf, sizeof(hBuf));
                            if( check_hbuf(hBuf) ){
                                hPos += currMetaSize + 1;
                            } else {
                                hPos += currMetaSize + 0xff000 + 1;
                            }
                            continue;
                        }

                        if( revHPos != 0 ){
                            hPos = revHPos;
                            revHPos = 0;
                            cncl = false;
                            found = false;
                            continue;
                        } else {
                            break;
                        }

                    } else {
                        throw std::runtime_error("vObtainMetaId Unknown Exception, Failed to get Meta ID");
                    }
                }
            } // while( hPos > (inHPos-MAX_BANK_SIZE*32) )

            if( !found && !cncl ){
               //reader.seekg(inHPos+firstMetaSize); // redundant seek?
               firstMetaSize = 0;
            }
        } // if hash of empty block found
    } // filepos for loop

    progress.finish();

    RecoBlocks toReconstruct(fMetas.size());

    for( size_t i = 0; i < fMetas.size(); i++ ){
        if( unneededMetas.find(i) != unneededMetas.end() && fMetas[i].empty() ){
            logger->info("Bank[{:x}] Skipping - unneeded, empty block", i);
            toReconstruct[i].recoNeeded = false;
            toReconstruct[i].unneeded = true;
            continue;
        }

        // not unneeded

        obtBankId = 0;
        if( fMetas[i].empty() ){
            logger->info("Bank[{:x}] Missing, trying to reconstruct", i);
            toReconstruct[i].recoNeeded = true;
            obtBankId = i;
        } else {
            logger->info("Bank[{:x}] Not missing", i);
            toReconstruct[i].recoNeeded = false;
            vObtainMetaID(fMetas[i], obtBankId);
            vGuessMetaIsVIB(fMetas[i], toReconstruct[i].isVib, toReconstruct[i].bLow, toReconstruct[i].bLow2, toReconstruct[i].bHigh, toReconstruct[i].bHigh2);
        }

        toReconstruct[i].metaPtr = i;
        toReconstruct[i].bank_id = obtBankId;
    }

    FileDescriptors vFiles;

    if( !toReconstruct.empty() ){
        logger->info("toReconstruct table dump:");
    }
    uint32_t MultiReco = 0;
    for( size_t i = 0; i < toReconstruct.size(); i++ ){
        if( toReconstruct[i].bank_id == 1 ){
            toReconstruct[i].recoNeeded = false;
        }
        if( i > 0 ){
            if( toReconstruct[i].recoNeeded ){
                if( !toReconstruct[i-1].unneeded && !toReconstruct[i-1].recoNeeded ){
                    toReconstruct[i].recoNeeded = false;
                    logger->info("Marking Bank {:x} as not needed for reconstruction", i);
                }
            }
        }
        if( toReconstruct[i].recoNeeded ){
            MultiReco++;
        }
    }

    uint64_t totalBl = 0;
    EmptyHashes emptyBl;
    BlockDescriptors bds; // used only when extracting files from MD

    for( size_t i = 0; i < toReconstruct.size(); i++ ){
        logger->info("    Bank[{:x}] {}", toReconstruct[i].bank_id, toReconstruct[i].recoNeeded ? "Recovery Needed!" : "Metadata OK");
        logger->info("    Bank[{:x}] blLow: {:x}, blHigh: {:x}, blLow2: {:x}, blHigh2: {:x}, {}", toReconstruct[i].bank_id, toReconstruct[i].bLow, toReconstruct[i].bHigh, toReconstruct[i].bLow2, toReconstruct[i].bHigh2, toReconstruct[i].isVib ? "IS VIB" : "is not VIB");
        if( toReconstruct[i].recoNeeded ){
            if( vReconstructMDTable(i, toReconstruct, fMetas, emptyBl, totalBl, vFiles, MultiReco, bds) ){
                logger->info("    Bank[{:x}] MD Repaired Successfully!", toReconstruct[i].bank_id);
            } else {
                logger->warn("    Bank[{:x}] MD Repair Failed!", toReconstruct[i].bank_id);
            }
        }
    }

    if( !fMetas.empty() ){
        std::filesystem::path f2name = get_out_pathname(m_fname, "METADATASCAN.bin");
        logger->info("Saving metadata to {} (without mirrors):", f2name);
        std::ofstream f2(f2name, std::ios::binary);
        if( !f2.is_open() ){
            throw std::runtime_error("failed to open " + f2name.string());
        }
        for( size_t i=0; i<fMetas.size(); i++){
            if( !fMetas[i].empty() ){
                logger->info("    Bank[{:x}] At Offset {:08x} Written {} ({:08x}) bytes", i, (size_t)f2.tellp(), fMetas[i].size(), fMetas[i].size());
                f2.write((const char*)fMetas[i].data(), fMetas[i].size());
            } else {
                logger->info("    Bank[{:x}] Empty", i);
            }
        }
    }

    if( !emptyBl.empty() || totalBl != 0 ){
        logger->info("----- EMPTY BLOCKS SUMMARY -----");
        logger->info("    TotalEmpty:  {} blocks, each 1MB", emptyBl.size());
        logger->info("    TotalBlocks: {} blocks, each 1MB", totalBl);
    }

    vDecompressAllBitmaps(vFiles);

    if( !vFiles.empty() ){
        logger->info("----- FILE TABLE SUMMARY -----");
        for( size_t i=0; i<vFiles.size(); i++ ){
            logger->info("[{:x}] Start: {}, Stop: {}, Disk Size: {:x}, ClustSize: {:x}, TotalBlCount: {:x}",
                i,
                vFiles[i].startPPI,
                vFiles[i].stopPPI,
                vFiles[i].diskSize,
                vFiles[i].clustSize,
                vFiles[i].totalBlSize
                );

            if( !vFiles[i].genBitmap.empty() ){
                logger->info("    GenBitmap: {} bytes", vFiles[i].genBitmap.size());
                logger->debug("{}", to_hexdump(vFiles[i].genBitmap));
            }
            if( !vFiles[i].arrMap.empty() ){
                logger->info("    arrMap: {} bytes", vFiles[i].arrMap.size());
                logger->debug("{}", to_hexdump(vFiles[i].arrMap));
            }
            if( !vFiles[i].unCompBitmap.empty() ){
                logger->info("    UnCompBitmap: {} bytes", vFiles[i].unCompBitmap.size());
                logger->debug("{}", to_hexdump(vFiles[i].unCompBitmap));
            }
        }
    }
    
    if( MultiReco>1 ){
        logger->info("[.] MultiMD procedure. MD Corrupted: {}", MultiReco);
    }
}
