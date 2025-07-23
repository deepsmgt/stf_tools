#pragma once

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <bitset>

#include "disassembler.hpp"
#include "format_utils.hpp"
#include "stf_enums.hpp"
#include "stf_inst.hpp"
#include "stf_inst_reader.hpp"
#include "file_utils.hpp"
#include "formatters.hpp"
#include "stf_region_iterators.hpp"

/**
 * \struct STFImemConfig
 *
 * Holds configuration options parsed from command line arguments
 */
struct STFImemConfig {
    bool java_trace = false;                                            /**< Trace is from a Java program */
    bool show_percentage = false;                                       /**< If true, show percentages */
    std::string trace_filename;                                         /**< Trace filename to read */
    std::string output_filename;                                        /**< Filename to write output to */
    uint32_t g_hw_tid = 0;                                              /**< If nonzero, will only include records with this hardware TID */
    uint32_t g_pid = 0;                                                 /**< If nonzero, will only include records with this PID */
    uint32_t g_tid = 0;                                                 /**< If nonzero, will only include records with this TID */
    uint64_t skip_count = 0;                                            /**< Skip this number of instructions */
    uint64_t keep_count = std::numeric_limits<uint64_t>::max();         /**< Stop generating IMEM after this many instructions */
    bool show_physpc = false;                                           /**< If true, show physical PCs */
    uint64_t runlength_count = std::numeric_limits<uint64_t>::max();    /**< Run length of the trace */
    uint64_t warmup_count = 0;                                          /**< Number of warmup instructions in the trace */
    bool track = false;                                                 /**< If true, track additional statistics */
    bool use_aliases = false;                                           /**< If true, use aliases when disassembling instructions */
    bool sort_output = false;                                           /**< If true, also output sorted version */
    bool skip_non_user = false;                                         /**< If true, skip non-user mode instructions */
    bool local_history = false;                                         /**< If true, print local history for branches, loads, stores */
    bool use_tracepoint_roi = false;                                    /**< If true, only process the ROI between tracepoints */
    uint32_t roi_start_opcode = 0;                                      /**< Overrides ROI tracepoint start opcode if nonzero */
    uint32_t roi_stop_opcode = 0;                                       /**< Overrides ROI tracepoint stop opcode if nonzero */
    bool use_pc_roi = false;                                            /**< If true, use PCs to detect ROI instead of tracepoint opcodes */
    uint64_t roi_start_pc = 0;                                          /**< Start PC for ROI detection */
    uint64_t roi_stop_pc = 0;                                           /**< Stop PC for ROI detection */
};

/**
 * \class IMemData
 *
 * Defines instruction access info
 *
 */
class IMemData {
    private:
        static constexpr uint32_t MAX_LHIST_ = 50;
        bool is_16bit_ = false;     /**< If true, this is a 16 bit instruction */
        uint32_t opcode_ = 0;       /**< Opcode */
        uint64_t phys_pc_ = 0;      /**< physical pc */
        uint64_t warmup_ = 0;       /**< warmup count */
        uint64_t runlength_ = 0;    /**< runlength count */
        uint64_t count_ = 0;        /**< Access count */
        bool is_loadstore_ = false; /**< Is load/store inst */
        uint64_t last_address_ = 0; /**< Last memory address */
        std::vector<int64_t> recent_strides_; /**< Recent load/store local strides */
        uint32_t recent_strides_idx_ = 0; /**< next index to writ stride */
        bool is_branch_ = false;    /**< Is branch inst */
        std::bitset<MAX_LHIST_> branch_lhr_; /**< Recent local branch history */
        uint32_t branch_lhr_idx_ = 0; /**< next index to local branch history */

    public:
        IMemData() = default;

        /**
         * Constructs an IMemData object
         * \param is_16bit If true, instruction is 16 bits
         * \param opcode Instruction opcode
         * \param phys_pc Physical PC
         * \param warmup warmup count
         * \param runlength runlength count
         * \param count access count
         * \param is_branch is a branch instruction
         * \param br_taken is a branch and branch was taken
         * \param mem_addr is a load/store and its memory address
         */
        IMemData(const bool is_16bit, const uint32_t opcode, const uint64_t phys_pc, const bool in_warmup,
                 const bool is_branch, const bool br_taken,
                 const uint64_t mem_addr) :
            is_16bit_(is_16bit),
            opcode_(opcode),
            phys_pc_(phys_pc),
            warmup_(in_warmup ? 1 : 0),
            runlength_(in_warmup ? 0 : 1),
            count_(1),
            is_loadstore_(mem_addr != 0),
            last_address_(mem_addr),
            recent_strides_(MAX_LHIST_, 0),
            is_branch_(is_branch)
        {
            if (is_branch_) {
                nextBranchHistory(br_taken);
            }
        }

        IMemData(const bool is_16bit, const uint32_t opcode, const uint64_t phys_pc, const bool in_warmup) :
            IMemData(is_16bit, opcode, phys_pc, in_warmup, false, false, 0)
        {
        }

        IMemData(const bool is_16bit, const uint32_t opcode, const uint64_t phys_pc, const bool in_warmup,
                 const bool is_branch, const bool br_taken) :
            IMemData(is_16bit, opcode, phys_pc, in_warmup, is_branch, br_taken, 0)
        {
        }

        IMemData(const bool is_16bit, const uint32_t opcode, const uint64_t phys_pc, const bool in_warmup,
                 const uint64_t mem_addr) :
            IMemData(is_16bit, opcode, phys_pc, in_warmup, false, false, mem_addr)
        {
        }

        /**
         * Checks whether the given opcode matches this instruction's opcode
         * \param opcode Opcode to check
         */
        inline bool opcodeMatch(const uint32_t opcode) const { return opcode_ == opcode; }

        /**
         * Gets the opcode
         */
        inline uint32_t getOpcode() const { return opcode_; }

        /**
         * Gets the physical PC
         */
        inline uint64_t getPhysPC() const { return phys_pc_; }

        /**
         * Gets the warmup count
         */
        inline uint64_t getWarmup() const { return warmup_; }

        /**
         * Gets the run length
         */
        inline uint64_t getRunLength() const { return runlength_; }

        /**
         * Gets the access count
         */
        inline uint64_t getCount() const { return count_; }

        /**
         * Gets the recent strides
         */
        inline const auto& getStrides() const { return recent_strides_; }

        /**
         * Gets the branch local history
         */
        inline const auto& getBranchLhr() const { return branch_lhr_; }

        /**
         * Increments the access count
         */
        inline void incCount() { ++count_; }

        /**
         * Increments the warmup count
         */
        inline void incWarmup() { ++warmup_; }

        /**
         * Incrememnts the run length count
         */
        inline void incRunLength() { ++runlength_; }

        /**
         * Is load or store instruction
         */
        inline bool isLoadStore() const { return is_loadstore_; }

        /**
         * Is branch instruction
         */
        inline bool isBranch() const { return is_branch_; }

        /**
         * Computes next stride
         */
        inline void nextStride(const uint64_t curr_addr) {
            recent_strides_[recent_strides_idx_] = static_cast<int64_t>(curr_addr - last_address_);
            recent_strides_idx_ = recent_strides_idx_ == (MAX_LHIST_ - 1) ? 0 : recent_strides_idx_ + 1;
            last_address_ = curr_addr;
        }

        /**
         * Next branch taken/notTaken
         */
        inline void nextBranchHistory(const bool taken) {
            // When this entry was created, it may not have been a taken branch
            is_branch_ = true;
            if (taken) {
                branch_lhr_.set(branch_lhr_idx_);
            } else {
                branch_lhr_.reset(branch_lhr_idx_);
            }
            branch_lhr_idx_++;
            if (branch_lhr_idx_ == MAX_LHIST_) {
                branch_lhr_idx_ = 0;
            }
        }

        /**
         * Gets the opcode size in bytes
         */
        inline uint32_t getOpcodeSize() const { return is_16bit_ ? 2 : 4; }
};

class IMemMapVec {
    private:
        static constexpr int TABLE_FIELD_WIDTH_ = 8;
        static constexpr int DEFAULT_COUNT_WIDTH_ = 20;

        mutable int count_field_width_ = DEFAULT_COUNT_WIDTH_;
        mutable int warmup_field_width_ = DEFAULT_COUNT_WIDTH_;
        mutable int run_length_field_width_ = DEFAULT_COUNT_WIDTH_;

        uint64_t max_count_ = 0; /**< Maximum single instruction count */
        uint64_t max_warmup_ = 0; /**< Maximum single instruction warmup */
        uint64_t max_run_length_ = 0; /**< Maximum single instruction run length */

        /**
         * Prints an IMemMapVec table field
         * \param str Name of field
         * \param width Width of field
         * \param end If true, this is the last field
         */
        inline static void printField_(OutputFileStream& os, const std::string_view str, const int width, const bool end = false) {
            const auto str_len = static_cast<int>(str.length());
            int padding = 0;
            bool odd = false;
            if(str_len < width) {
                const auto diff = width - str_len;
                odd = diff & 1;
                padding = diff >> 1;
            }
            stf::format_utils::formatWidth(os, str, padding + str_len + odd, '-');
            if(STF_EXPECT_TRUE(!end)) {
                stf::format_utils::formatWidth(os, "||", padding + 2, '-');
            }
            else {
                stf::format_utils::formatWidth(os, "", padding, '-');
            }
        }

        /**
         * Calculates and formats a percentage from a numerator and denominator
         * \param numerator Numerator
         * \param denominator Denominator
         */
        template<typename NumT,
                 typename DenomT,
                 int PERCENT_WIDTH = 7,
                 int PERCENT_PRECISION = 4,
                 int FIELD_WIDTH = TABLE_FIELD_WIDTH_,
                 typename OStream = std::ostream>
        inline static void printPercentage_(OStream& os, const NumT numerator, const DenomT denominator) {
            static_assert(PERCENT_WIDTH >= PERCENT_PRECISION, "PERCENT_WIDTH must be >= PERCENT_PRECISION");
            static_assert(FIELD_WIDTH >= PERCENT_WIDTH, "FIELD_WIDTH must be >= PERCENT_WIDTH");

            stf::format_utils::formatPercent(os,
                                             static_cast<float>(numerator)/static_cast<float>(denominator),
                                             PERCENT_WIDTH,
                                             PERCENT_PRECISION);
            stf::format_utils::formatSpaces(os, FIELD_WIDTH - PERCENT_WIDTH);
        }

        template<typename NumT, typename DenomT, typename OStream = std::ostream>
        inline static void printSortedPercentage_(OStream& os, const NumT numerator, const DenomT denominator) {
            printPercentage_<NumT, DenomT, 5, 1, 7>(os, numerator, denominator);
        }

        /**
         * Prints a 0%
         */
        inline static void printPercentage_(OutputFileStream& os) {
            printPercentage_(os, 0, 1);
        }

        class SortedMapKey {
            private:
                uint64_t count_ = 0;
                uint64_t pc_ = 0;

            public:
                SortedMapKey(const uint64_t count, const uint64_t pc) :
                    count_(count),
                    pc_(pc)
                {
                }

                inline bool operator<(const SortedMapKey& rhs) const {
                    return (count_ < rhs.count_) || ((count_ == rhs.count_) && (pc_ > rhs.pc_));
                }

                inline uint64_t getCount() const {
                    return count_;
                }

                inline uint64_t getPC() const {
                    return pc_;
                }
        };

    protected:
        /**
         * \typedef IMemMap
         * Underlying map storage
         */
        using IMemMap = std::map<uint64_t, IMemData>;
        std::vector<IMemMap> imem_mapvec_; /**< Vector of IMemMap objects */
        std::vector<IMemMap>::iterator itv_; /**< Iterator into imem_mapvec_ */

        uint64_t inst_count_ = 0; /**< Instruction count */
        bool is_rv64_ = false; /**< If true, trace is RV64 */
        stf::ISA inst_set_ = stf::ISA::RESERVED; /**< Instruction set */
        stf::INST_IEM iem_ = stf::INST_IEM::STF_INST_IEM_INVALID; /**< Instruction encoding */
        std::string isa_string_;

        /**
         * Constructs an IMemMap entry
         * \param key Key for the map entry
         * \param inst Instruction
         * \param physpc Physical PC
         * \param in_warmup if true, instruction is in warmup period
         * \param is_branch instruction is a branch
         * \param br_taken is a branch and branch was taken
         * \param add memory address of load/store instruction
         */
        inline static IMemMap::value_type createIMem_(const uint64_t key,
                                                      const stf::STFInst& inst,
                                                      const uint64_t physpc,
                                                      const bool in_warmup) {
            return std::make_pair(key, IMemData(inst.isOpcode16(), inst.opcode(), physpc, in_warmup));
        }
        inline static IMemMap::value_type createIMem_(const uint64_t key,
                                                      const stf::STFInst& inst,
                                                      const uint64_t physpc,
                                                      const bool in_warmup,
                                                      const uint64_t addr) {
            return std::make_pair(key, IMemData(inst.isOpcode16(), inst.opcode(), physpc, in_warmup, addr));
        }
        inline static IMemMap::value_type createIMem_(const uint64_t key,
                                                      const stf::STFInst& inst,
                                                      const uint64_t physpc,
                                                      const bool in_warmup,
                                                      const bool is_branch,
                                                      const bool br_taken) {
            return std::make_pair(key, IMemData(inst.isOpcode16(), inst.opcode(), physpc, in_warmup, is_branch, br_taken));
        }

        inline void printIMemItem_(OutputFileStream& os,
                                   const STFImemConfig& config,
                                   const stf::Disassembler& dis,
                                   const IMemMap::const_iterator& imem_it) const {
            const auto& cur = *imem_it;
            const uint64_t inst_pc = cur.first;
            const uint32_t opcode = cur.second.getOpcode();
            const uint64_t count = cur.second.getCount();
            const uint64_t warmup = cur.second.getWarmup();
            const uint64_t runlen = cur.second.getRunLength();
            const uint64_t physpc = cur.second.getPhysPC();

            if (config.show_percentage) {
                printPercentage_(os, count, inst_count_);
                if (config.track) {
                    if (config.warmup_count != 0) {
                        printPercentage_(os, warmup, config.warmup_count);
                    }
                    else {
                        printPercentage_(os);
                    }
                    if (config.runlength_count != 0) {
                        printPercentage_(os, runlen, config.runlength_count);
                    }
                    else {
                        printPercentage_(os);
                    }
                }
            }
            stf::format_utils::formatDec(os, count, count_field_width_, ' ');
            if (config.track) {
                stf::format_utils::formatSpaces(os, 2);
                stf::format_utils::formatDec(os, warmup, warmup_field_width_, ' ');
                stf::format_utils::formatSpaces(os, 2);
                stf::format_utils::formatDec(os, runlen, run_length_field_width_, ' ');
            }
            stf::format_utils::formatSpaces(os, 2);
            stf::format_utils::formatHex(os, inst_pc, stf::format_utils::VA_WIDTH);
            if (config.show_physpc) {
                os << ':';
                stf::format_utils::formatHex(os, physpc, stf::format_utils::VA_WIDTH);
            }

            stf::format_utils::formatSpaces(os, 2);
            dis.printOpcode(os, opcode);
            stf::format_utils::formatSpaces(os, 1);
            dis.printDisassembly(os, inst_pc, opcode);

            os << std::endl;
        }

        inline void printSortedIMemItem_(OutputFileStream& os,
                                         const STFImemConfig& config,
                                         const stf::Disassembler& dis,
                                         const IMemMap::const_iterator& imem_it,
                                         const uint64_t cumulative_count,
                                         const int count_comma_width) const {
            const auto& cur = *imem_it;
            const uint64_t inst_pc = cur.first;
            const uint32_t opcode = cur.second.getOpcode();
            const uint64_t count = cur.second.getCount();
            const uint64_t physpc = cur.second.getPhysPC();

            // Add extra padding for commas
            stf::format_utils::formatDec(CommaFormatter(os), count, count_comma_width, ' ');
            stf::format_utils::formatSpaces(os, 2);
            printSortedPercentage_(os, count, inst_count_);
            printSortedPercentage_(os, cumulative_count, inst_count_);
            stf::format_utils::formatHex(os, inst_pc, stf::format_utils::VA_WIDTH);
            if (config.show_physpc) {
                os << ':';
                stf::format_utils::formatHex(os, physpc, stf::format_utils::VA_WIDTH);
            }

            stf::format_utils::formatSpaces(os, 2);
            dis.printOpcode(os, opcode);
            stf::format_utils::formatSpaces(os, 2);
            dis.printDisassembly(os, inst_pc, opcode);

            if (config.local_history) {
                if (cur.second.isLoadStore()) {
                    // Print additional info (e.g. local address stride)
                    os << "    LStrides={";
                    const auto& strides = cur.second.getStrides();
                    for(auto stride = strides.begin(); stride != strides.end(); ++stride) {
                        os << *stride << ",";
                    }
                    os << "}";
                } else if (cur.second.isBranch()) {
                    os << "    LHR={";
                    const auto& lhr = cur.second.getBranchLhr();
                    for(uint32_t i = 0; i < lhr.size(); ++i) {
                        os << (lhr[i] ? "1" : "0");
                    }
                    os << "}";
                }
            }

            os << std::endl;
        }

        inline void incCount_(IMemData& data) {
            data.incCount();
            max_count_ = std::max(max_count_, data.getCount());
        }

        inline void incWarmup_(IMemData& data) {
            data.incWarmup();
            max_warmup_ = std::max(max_warmup_, data.getWarmup());
        }

        inline void incRunLength_(IMemData& data) {
            data.incRunLength();
            max_run_length_ = std::max(max_run_length_, data.getRunLength());
        }

        inline void nextStride_(IMemData& data, const uint64_t curr_addr) {
            data.nextStride(curr_addr);
        }

        inline void nextBranchHistory_(IMemData& data, const bool taken) {
            data.nextBranchHistory(taken);
        }

    public:
        IMemMapVec() :
            imem_mapvec_(1),
            itv_(imem_mapvec_.begin())
        {
        }

        virtual ~IMemMapVec() = default;

        /**
         * Processes a trace
         * \param config Configuration
         */
        virtual void processTrace(const STFImemConfig& config) = 0;

        /**
         * Prints the result of processing a trace
         * \param config Configuration
         */
        void print(const STFImemConfig& config) const {
            // Now print out the map
            uint64_t prev_pc = 0;
            uint32_t prev_size = 0;
            uint32_t map_num = 1;
            static constexpr int NOPHYSPC_WIDTH = 16;
            static constexpr int PHYSPC_WIDTH = NOPHYSPC_WIDTH + 17;

            OutputFileStream os(config.output_filename);

            count_field_width_ = std::max(TABLE_FIELD_WIDTH_, numDecimalDigits<int>(max_count_));
            warmup_field_width_ = std::max(TABLE_FIELD_WIDTH_, numDecimalDigits<int>(max_warmup_));
            run_length_field_width_ = std::max(TABLE_FIELD_WIDTH_, numDecimalDigits<int>(max_run_length_));

            // Print header
            if (config.track) {
                os << "============ CONFIG  ============" << std::endl
                   << "original trace: " << config.trace_filename << std::endl
                   << "warmup: " << config.warmup_count << std::endl
                   << "skip non-user: " << std::boolalpha << config.skip_non_user << std::endl;

                if (config.show_percentage) {
                    printField_(os, "total%", TABLE_FIELD_WIDTH_);
                    printField_(os, "warm%", TABLE_FIELD_WIDTH_);
                    printField_(os, "run%", TABLE_FIELD_WIDTH_);
                }
                printField_(os, "total", count_field_width_);
                printField_(os, "warm", warmup_field_width_);
                printField_(os, "runl", run_length_field_width_);
                printField_(os, "instpc", config.show_physpc ? PHYSPC_WIDTH : NOPHYSPC_WIDTH);
                printField_(os, "opcode", TABLE_FIELD_WIDTH_);
                printField_(os, "disasm", TABLE_FIELD_WIDTH_, true);
            }

            using SortedVector = std::vector<IMemMap::const_iterator>;
            SortedVector current_block;

            std::multimap<SortedMapKey, SortedVector> sorted_map;
            bool first = true;
            stf::Disassembler dis(findElfFromTrace(config.trace_filename), inst_set_, iem_, isa_string_, config.use_aliases);
            uint64_t block_count = 0;

            for (auto it = imem_mapvec_.rbegin(); it != imem_mapvec_.rend(); it ++) {
                os << std::endl << "============ MAP " << map_num;
                if (is_rv64_) {
                    os << " IEM:RV64 ============";
                }
                else {
                    os << " IEM:RV32 ============";
                }
                os << std::endl;

                for (auto imem_it = it->begin(); imem_it != it->end(); ++imem_it) {
                    const auto& cur = *imem_it;
                    const uint64_t inst_pc = cur.first;
                    const uint64_t count = cur.second.getCount();

                    if(STF_EXPECT_FALSE(first || (prev_pc + prev_size != inst_pc))) {
                        if(STF_EXPECT_FALSE(first)) {
                            first = false;
                        }
                        else {
                            if(!config.sort_output) {
                                os << "..." << std::endl;
                            }
                            if(config.sort_output) {
                                sorted_map.emplace(std::piecewise_construct,
                                                   std::forward_as_tuple(block_count, current_block.front()->first),
                                                   std::forward_as_tuple(std::move(current_block)));
                                current_block.clear();
                                block_count = 0;
                            }
                        }
                    }
                    if(config.sort_output) {
                        block_count += count;
                        current_block.emplace_back(imem_it);
                    }

                    if(!os.isStdout() || !config.sort_output) {
                        printIMemItem_(os, config, dis, imem_it);
                    }
                    prev_pc = inst_pc;
                    prev_size = cur.second.getOpcodeSize();
                }

                map_num++;
            }

            if(config.sort_output && !current_block.empty()) {
                sorted_map.emplace(std::piecewise_construct,
                                   std::forward_as_tuple(block_count, current_block.front()->first),
                                   std::forward_as_tuple(std::move(current_block)));
            }

            if(config.sort_output) {
                if(STF_EXPECT_FALSE(sorted_map.empty())) {
                    std::cerr << "Warning: generated imem was empty! Skipping generation of sorted imem." << std::endl;
                    return;
                }
                std::string sorted_filename;
                if(os.isStdout()) {
                    sorted_filename = "-";
                }
                else {
                    static constexpr std::string_view imem_ext(".imem");

                    const auto ext_idx = config.output_filename.rfind(imem_ext);
                    if(ext_idx != std::string::npos && ext_idx == (config.output_filename.size() - imem_ext.size())) {
                        sorted_filename = config.output_filename.substr(0, ext_idx) + ".s_imem";
                    }
                    else {
                        sorted_filename = config.output_filename + ".s_imem";
                    }
                }

                OutputFileStream sorted_os(sorted_filename);

                if(sorted_os.isStdout()) {
                    sorted_os << "-----------------------------------------" << std::endl;
                }
                CommaFormatter(sorted_os) << "Total inst count = " << inst_count_ << std::endl
                                          << "Max count        = " << max_count_ << std::endl;

                uint64_t cumulative_count = 0;
                const int count_comma_width = CommaFormatter::formattedWidth(count_field_width_);

                for(auto it = sorted_map.rbegin(); it != sorted_map.rend(); ++it) {
                    sorted_os << "-------------------------";
                    const auto count = it->first.getCount();
                    printPercentage_<decltype(count),
                                     decltype(inst_count_),
                                     6, 1, 7>(sorted_os, count, inst_count_);
                    CommaFormatter(sorted_os) << "- " << count << " inst, "
                                              << it->second.size() << " addr" << std::endl;
                    for(const auto& imem_it: it->second) {
                        cumulative_count += imem_it->second.getCount();
                        printSortedIMemItem_(sorted_os, config, dis, imem_it, cumulative_count, count_comma_width);
                    }
                }

                stf_assert(cumulative_count == inst_count_,
                           "Not all blocks were included in sorted output! cumulative_count = " <<
                           cumulative_count << ", inst_count_ = " << inst_count_);
            }
        }
};

/**
 * \class IMemMapVec
 * Processes an STF and collects an imem
 */
template<typename ImplType>
class IMemMapVecIntf : public IMemMapVec {
    private:
        template<typename IteratorType, typename StartStopType = std::nullopt_t>
        void processTrace_(const STFImemConfig& config, const StartStopType start_point = std::nullopt, const StartStopType stop_point = std::nullopt) {
            stf::STFInstReader stf_reader(config.trace_filename, config.skip_non_user);

            inst_set_ = stf_reader.getISA();
            iem_ = stf_reader.getInitialIEM();
            isa_string_ = stf_reader.getISAExtendedInfo();
            is_rv64_ = iem_ == stf::INST_IEM::STF_INST_IEM_RV64;

            for (auto it = stf::getStartIterator<IteratorType>(stf_reader, config.skip_count, start_point, stop_point); it != stf_reader.end(); ++it) {
                const auto& inst = *it;

                if (STF_EXPECT_FALSE(!inst.valid())) {
                    std::cerr << "ERROR: " << inst.index() << " invalid instruction ";
                    stf::format_utils::formatHex(std::cerr, inst.opcode());
                    std::cerr << " PC ";
                    stf::format_utils::formatHex(std::cerr, inst.pc());
                    std::cerr << std::endl;
                }

                if (STF_EXPECT_FALSE(config.g_hw_tid != 0 && config.g_hw_tid != inst.hwtid())) {
                    continue;
                }
                if (STF_EXPECT_FALSE(config.g_pid != 0 && config.g_pid != inst.pid())) {
                    continue;
                }
                if (STF_EXPECT_FALSE(config.g_tid != 0 && config.g_tid != inst.tid())) {
                    continue;
                }

                // ignore faulting instructions since they will be replayed
                if (STF_EXPECT_FALSE(inst.isFault())) {
                    continue;
                }

                static_cast<ImplType*>(this)->count_impl(config, inst);

                ++inst_count_;

                if (STF_EXPECT_FALSE(inst_count_ >= config.keep_count)) {
                    break;
                }
            }
        }

    public:
        /**
         * Processes a trace
         * \param config Configuration
         */
        void processTrace(const STFImemConfig& config) final {
            if(config.use_tracepoint_roi) {
                if(config.use_pc_roi) {
                    processTrace_<stf::STFPCIterator>(config, config.roi_start_pc, config.roi_stop_pc);
                }
                else {
                    processTrace_<stf::STFTracepointIterator>(config, config.roi_start_opcode, config.roi_stop_opcode);
                }
            }
            else {
                processTrace_<stf::STFInstReader::iterator>(config);
            }
        }
};

/**
 * \class IMem
 *
 * Specialization of IMemMapVec for regular executables
 */
class IMem : public IMemMapVecIntf<IMem> {
    public:
        inline void count_impl(const STFImemConfig& config, const stf::STFInst& inst) {
            const uint64_t key = inst.pc();
            static const uint64_t physpc = 0;
            auto cur = itv_->find(key);
            // non Java trace
            const bool in_warmup = inst_count_ < config.warmup_count;
            const bool is_load_store_inst = inst.isLoad() || inst.isStore();
            uint64_t addr = 0;
            if (inst.isLoad()) {
                for(const auto& m: inst.getMemoryReads()) {
                    addr = m.getAddress();
                }
            } else if (inst.isStore()) {
                for(const auto& m: inst.getMemoryWrites()) {
                    addr = m.getAddress();
                }
            }
            // Need to use STFDecoder if we want to capture branches that are never taken
            const bool is_branch = inst.isTakenBranch();
            const bool br_taken = inst.isTakenBranch();

            if (cur == itv_->end()) {
                // Key not found
                if (is_load_store_inst) {
                    itv_->insert(createIMem_(key, inst, physpc, in_warmup, addr));
                } else if (is_branch) {
                    itv_->insert(createIMem_(key, inst, physpc, in_warmup, is_branch, br_taken));
                } else {
                    itv_->insert(createIMem_(key, inst, physpc, in_warmup));
                }
            }
            else if (STF_EXPECT_TRUE(cur->second.opcodeMatch(inst.opcode()))) {
                // Key and opcode found
                incCount_(cur->second);
                if (STF_EXPECT_FALSE(in_warmup)) {
                    incWarmup_(cur->second);
                }
                else if (STF_EXPECT_TRUE(inst_count_ < config.runlength_count)) {
                    incRunLength_(cur->second);
                }
                if (is_load_store_inst) {
                    nextStride_(cur->second, addr);
                } else if (is_branch) {
                    nextBranchHistory_(cur->second, br_taken);
                }
            }
            else {
                // different opcode
                std::cerr << "WARN : 0x";
                stf::format_utils::formatHex(std::cerr, key);
                std::cerr << " two opcodes ";
                stf::format_utils::formatHex(std::cerr, cur->second.getOpcode());
                std::cerr << ' ';
                stf::format_utils::formatHex(std::cerr, inst.opcode());
                std::cerr << std::endl;
            }
        }
};

/**
 * \class JavaIMem
 *
 * Specialization of IMemMapVec for Java executables
 */
class JavaIMem : public IMemMapVecIntf<JavaIMem> {
    public:
        inline void count_impl(const STFImemConfig& config, const stf::STFInst& inst) {
            const uint64_t key = inst.pc();
            static const uint64_t physpc = 0;
            auto cur = itv_->find(key);
            const bool in_warmup = inst_count_ < config.warmup_count;
            if ((cur != itv_->end()) && cur->second.opcodeMatch(inst.opcode())) {
                // Key and opcode found
                incCount_(cur->second);
                if (STF_EXPECT_FALSE(in_warmup)) {
                    incWarmup_(cur->second);
                }
                else if (STF_EXPECT_TRUE(inst_count_ < config.runlength_count)) {
                    incRunLength_(cur->second);
                }
            }
            else {
                // may use multiple maps for Java trace
                bool found = false;
                auto itt = imem_mapvec_.end();
                auto it = imem_mapvec_.begin();
                while (it != imem_mapvec_.end()) {
                    cur = it->find(key);
                    if (cur != it->end()) {
                        // Key found
                        if (cur->second.opcodeMatch(inst.opcode())) {
                            // same key and opcode
                            found = true;
                            break;
                        }
                        // else, different opcode, check other maps
                    }
                    else {
                        // Key not found
                        itt = it;
                    }
                    it ++;
                }

                if (found) {
                    incCount_(cur->second);
                    if (STF_EXPECT_FALSE(in_warmup)) {
                        incWarmup_(cur->second);
                    }
                    else if (STF_EXPECT_TRUE(inst_count_ < config.runlength_count)) {
                        incRunLength_(cur->second);
                    }
                    itv_ = it;
                }
                else {
                    if (itt == imem_mapvec_.end()) {
                        // all maps contain the key but different opcodes, create a new map
                        itv_ = imem_mapvec_.insert(imem_mapvec_.begin(), IMemMap());
                    }
                    else if (itv_->find(key) != itv_->end()) {
                        itv_ = itt;
                    }

                    itv_->insert(createIMem_(key, inst, physpc, in_warmup));
                }
            }
        }
};
