/**
 * @file Meta_process.cpp
 * @brief Implementation of metadata processing and shrinking operations.
 *
 * This file provides functionality to process metadata descriptor CSV files and
 * corresponding binary metadata files, filtering and shrinking them by removing
 * unnecessary or duplicate entries. It performs validation, deduplication, and
 * compaction of metadata structures for more efficient storage and processing.
 */

#include "Meta_process.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <filesystem>

#include "core/structs.hpp"

const uint8_t array1Skip[] = {0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t array0Skip[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
                             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/**
 * @brief Constructs a Metaprocess object for metadata shrinking.
 *
 * @param edit2Text Path to descriptor CSV file.
 * @param edit3Text Path to metadata binary file.
 */
Metaprocess::Metaprocess(const std::string& edit2Text, const std::string& edit3Text)
    : edit2Text(edit2Text), edit3Text(edit3Text) {
    szHt.clear();
    m_dummyBuffer.resize(0x2000, 0);
    m_dummyBuffer[0] = 0x02;
}

bool Metaprocess::CompareMem(const void* buf1, const void* buf2, size_t len) {
    return memcmp(buf1, buf2, len) == 0;
}

uint64_t Metaprocess::Hex2Dec64(const std::string& hex) {
    uint64_t result;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> result;
    return result;
}

std::string Metaprocess::IntToHex(uint64_t value, int width) {
    std::stringstream ss;
    ss << std::setw(width) << std::setfill('0') << std::hex << std::uppercase << value;
    return ss.str();
}

std::string Metaprocess::IntToStr(int64_t value) {
    return std::to_string(value);
}

void Metaprocess::szGetCsvN(const std::string& sz, std::vector<std::string>& ot) {
    std::stringstream ss(sz);
    std::string item;
    ot.clear();
    while (std::getline(ss, item, ';')) {
        ot.push_back(item);
    }
}

void Metaprocess::szPutCsvN(const std::vector<std::string>& ot, std::string& sz) {
    sz.clear();
    for (const auto& item : ot) {
        sz += item + ";";
    }
}

void Metaprocess::Execute() {
    std::ifstream f(edit2Text);
    std::ifstream ff(edit3Text, std::ios::binary);
    std::ofstream fnn(edit3Text + "-Shrinked.bin", std::ios::binary);
    std::ofstream fn(edit2Text + "-Shrinked.csv");
    
    if (!f || !ff || !fnn || !fn) {
        std::cerr << "Error opening files" << std::endl;
        return;
    }

    uint64_t qoff = 0;
    std::string szBuf;
    size_t recordCount = 0;
    
    while (std::getline(f, szBuf)) {
        std::vector<std::string> ot;
        szGetCsvN(szBuf, ot);
        
        if (ot.size() >= 4) {
            bool Delete = false;
            uint32_t mdSz = Hex2Dec64(ot[3]);
            uint64_t mdPtr = qoff;
            qoff += mdSz;
            std::string szHash = ot[4];
            
            uint64_t vrPoints = 0;
            uint64_t maxVrPoints = ((mdSz - 0x1000) / 0x20) - 1;
            
            buf_t buf(mdSz);
            ff.seekg(mdPtr);
            ff.read(reinterpret_cast<char*>(buf.data()), mdSz);
            
            for (uint32_t i = 0x1020; i < mdSz; i += 0x20) {
                if (CompareMem(&buf[i], array1Skip, sizeof(array1Skip))) {
                    vrPoints++;
                }
                else if (CompareMem(&buf[i], array0Skip, sizeof(array0Skip))) {
                    maxVrPoints--;
                }
            }
            
            Delete = Delete || (vrPoints > (maxVrPoints * 0.51));
            
            for (const auto& hash : szHt) {
                if (hash == szHash) {
                    Delete = true;
                    break;
                }
            }
            
            bool isVib = false;
            uint64_t blLow = 0, blHigh = 0, blLow2 = 0, blHigh2 = 0;
            
            try {
                vGuessMetaIsVIB1(buf, isVib, blLow, blLow2, blHigh, blHigh2);

                ot.resize(13);
                std::stringstream ss;
                ss << std::setfill('0') << std::setw(8) << std::hex << std::uppercase << fnn.tellp();
                ot[2] = ss.str();
                
                ot[4] = szHash;
                ot[5] = "";
                ot[6] = "";
                ot[7] = "";
                ot[8] = "";
                
                ot[9] = IntToStr(blLow);
                ot[10] = IntToStr(blHigh);
                ot[11] = IntToStr(blLow2);
                ot[12] = IntToStr(blHigh2);
                
                Delete = Delete || isVib;
                
                if (!Delete) {
                    szHt.push_back(szHash);
                    fnn.write(reinterpret_cast<const char*>(buf.data()), buf.size());
                    
                    uint32_t mid;
                    if (vGuessMetaID1(buf, mid)) {
                        std::cout << IntToHex(mdPtr, 8) << ":" << IntToHex(mdSz, 8) 
                                 << " MID:" << IntToStr(mid) << std::endl;
                        ot[8] = IntToStr(mid);
                    }
                    
                    std::string csvLine;
                    szPutCsvN(ot, csvLine);
                    fn << csvLine << std::endl;
                    fn.flush();
                }
                
                recordCount++;

            } catch (const std::exception& e) {
                std::cerr << "Error processing record: " << e.what() << std::endl;
                continue;
            }
        }
    }

    f.close();
    ff.close();
    fn.close();
    fnn.close();

    try {
        processMetadata();
    } catch (const std::exception& e) {
        std::cerr << "Error in metadata processing: " << e.what() << std::endl;
    }
}

void Metaprocess::processMetadata() {
    readCSV();
    processMID0Entries();
}

void Metaprocess::readCSV() {
    std::ifstream csvFile(edit2Text + "-Shrinked.csv");
    if (!csvFile.is_open()) {
        throw std::runtime_error("Failed to open CSV file: " + edit2Text + "-Shrinked.csv");
    }

    std::string line;
    while (std::getline(csvFile, line)) {
        if (line.empty()) continue;

        auto fields = splitCSV(line);
        if (fields.size() < 13) continue;

        VMetaCx meta;
        try {
            meta.Offset = hexToInt(fields[2]);
            meta.Size = hexToInt(fields[3]);
            meta.MID = std::stoi(fields[8]);
            meta.blLow = std::stoull(fields[9]);
            meta.blHigh = std::stoull(fields[10]);
            meta.PhyOffset = hexToInt(fields[1]);
            meta.blLow2 = std::stoull(fields[11]);
            meta.blHigh2 = std::stoull(fields[12]);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to parse line: " << line << std::endl;
            continue;
        }

        m_metaCx.push_back(meta);
    }
}

void Metaprocess::processMID0Entries() {
    std::ifstream dataFile(edit3Text + "-Shrinked.bin", std::ios::binary);
    if (!dataFile.is_open()) {
        throw std::runtime_error("Failed to open data file");
    }

    for (size_t n = 0; n < m_metaCx.size(); ++n) {
        if (m_metaCx[n].MID != 0) continue;

        uint64_t inputPhyOff = m_metaCx[n].PhyOffset;
        std::string outputPath = getOutputPath(inputPhyOff);
        
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile.is_open()) {
            throw std::runtime_error("Failed to create output file: " + outputPath);
        }

        outFile.put(XBYTE);

        std::vector<char> buffer(m_metaCx[n].Size);
        dataFile.seekg(m_metaCx[n].Offset);
        dataFile.read(buffer.data(), m_metaCx[n].Size);
        outFile.write(buffer.data(), m_metaCx[n].Size);

        uint32_t prevMid = m_metaCx[n].MID;
        uint64_t prevBlHigh = m_metaCx[n].blHigh;
        uint64_t prevBlHigh2 = m_metaCx[n].blHigh2;

        std::cout << "Processing block at Offset: 0x" << std::hex << m_metaCx[n].Offset
                  << ", MID: " << std::dec << prevMid
                  << ", BlHigh: " << prevBlHigh
                  << ", BlHigh2: " << prevBlHigh2 << std::endl;

        bool done = false;
        while (!done) {
            done = true;
            for (size_t i = 0; i < m_metaCx.size(); ++i) {
                const auto& current = m_metaCx[i];
                
                bool sequentialMid = (current.MID == prevMid + 1) || (current.MID == prevMid + 2);
                bool validBlocks = (current.blLow == prevBlHigh + 1) || 
                                 (current.blLow2 - prevBlHigh2 <= 0x102000);
                bool progressiveBlocks = current.blLow > prevBlHigh;

                if (sequentialMid && validBlocks && progressiveBlocks) {
                    if (current.MID == prevMid + 2) {
                        outFile.write(reinterpret_cast<const char*>(m_dummyBuffer.data()), m_dummyBuffer.size());
                    }

                    buffer.resize(current.Size);
                    dataFile.seekg(current.Offset);
                    dataFile.read(buffer.data(), current.Size);
                    outFile.write(buffer.data(), current.Size);

                    prevMid = current.MID;
                    prevBlHigh = current.blHigh;
                    prevBlHigh2 = current.blHigh2;

                    std::cout << "Appending block at Offset: 0x" << std::hex << current.Offset
                              << ", MID: " << std::dec << prevMid
                              << ", BlHigh: " << prevBlHigh
                              << ", BlHigh2: " << prevBlHigh2 << std::endl;

                    done = false;
                    break;
                }
            }
        }

        outFile.flush();
        outFile.close();
        std::cout << "Generated: " << outputPath << std::endl;
    }
}

std::string Metaprocess::getOutputPath(uint64_t physOffset) {
    std::filesystem::path csvPath(edit2Text);
    auto outputPath = csvPath.parent_path() / (getHexString(physOffset) + "-ReadyMeta.bin");
    return outputPath.string();
}

std::vector<std::string> Metaprocess::splitCSV(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    
    while (std::getline(ss, item, ';')) {
        result.push_back(item);
    }
    return result;
}

std::string Metaprocess::getHexString(uint64_t value, int width) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
    return ss.str();
}

uint64_t Metaprocess::hexToInt(const std::string& hex) {
    uint64_t value;
    std::stringstream ss;
    ss << std::hex << hex;
    ss >> value;
    return value;
}

bool vGuessMetaID1(buf_t& vm, uint32_t& mpidOut) {
    uint64_t sum = 0;
    uint32_t cnt = 0;
    uint32_t numBlocks = static_cast<uint32_t>(vm.size() / 0x1000)-1;
    
    for (uint32_t i = 0; i < numBlocks; ++i) {
        size_t midIndex = i * 0x1000 + 0x1008;
        size_t mpidIndex = i * 0x1000 + 0x100C;
        
        if (midIndex + sizeof(uint32_t) > vm.size() || mpidIndex + sizeof(uint32_t) > vm.size()) {
            throw std::out_of_range("vGuessMetaID : index out of bounds while accessing vm.");
        }
        
        uint32_t mid = *(uint32_t*) &vm[midIndex];
        uint32_t mpid = *(uint32_t*) &vm[mpidIndex];
        
        if (mid == i) {
            if ((mpid & 0xFFFFF000) == 0) {
                cnt++;
                sum += mpid;
            }
        }
    }
    
    if (cnt > 1) {
        mpidOut = sum / cnt;
        std::cout << "vGuessMetaID: " << std::hex << mpidOut << std::endl;
    }
    
    return true;
}

bool vGuessMetaIsVIB1(buf_t& vm, bool& isVib, uint64_t& blLow, uint64_t& blLow2, uint64_t& blHigh, uint64_t& blHigh2) {
    bool result = false;
    size_t mCnt = vm.size() / 0x1000 - 1;
    
    isVib = false;
    blLow = 0;
    blLow2 = 0;
    blHigh = 0;
    blHigh2 = 0;
    
    for (size_t i = 0; i < mCnt; i++) {
        VBlockDesc *vBl = (VBlockDesc*)&vm[(i+1)*0x1000];
        size_t cnt = 0;
        
        if (vBl->size == BLOCK_SIZE) {
            while (vBl->size == BLOCK_SIZE && cnt < 0x1000) {
                if (vBl->size == BLOCK_SIZE && vBl->vib_offset > 0 && vBl->type == 0) {
                    isVib = true;
                }
                if (blLow == 0 && vBl->id != 0 && vBl->type == 0) {
                    blLow = vBl->id;
                }
                if (vBl->id != 0 && vBl->type == 0) {
                    blHigh = vBl->id;
                }
                vBl++;
                cnt += sizeof(VBlockDesc);
            }
        } 
        else {
            BlockDescriptor *vBl2 = (BlockDescriptor*)&vm[(i+1)*0x1000];
            if (vBl2->valid() && vBl2->dedup == 1) {
                while (cnt < 0x1000) {
                    if (blLow2 == 0 && vBl2->allocSize != 0) {
                        blLow2 = vBl2->offset;
                    }
                    if (vBl2->allocSize != 0) {
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
