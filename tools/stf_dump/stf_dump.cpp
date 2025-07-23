
// <STF_dump> -*- C++ -*-

/**
 * \brief  This tool prints out the content of a STF trace file
 *
 */

#include <cstdlib>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include "command_line_parser.hpp"
#include "disassembler.hpp"
#include "print_utils.hpp"
#include "stf_dump.hpp"
#include "stf_inst_reader.hpp"
#include "stf_page_table.hpp"
#include "tools_util.hpp"
#include "stf_region_iterators.hpp"

static STFDumpConfig parseCommandLine (int argc, char **argv) {
    // Parse options
    STFDumpConfig config;
    trace_tools::CommandLineParser parser("stf_dump");

    parser.addFlag('c', "concise mode - only dumps PC information and disassembly");
    parser.addFlag('u', "only dump user-mode instructions");
    parser.addFlag('p', "show physical addresses");
    parser.addFlag('P', "show the PTE entries");
    parser.addFlag('A', "use aliases for disassembly (only used by binutils disassembler)");
    parser.addFlag('m', "Enables cross checking trace instruction opcode against opcode from symbol table file <*_symTab.yaml>. Applicable only when '-y' flag is enabled");
    parser.addFlag('s', "N", "start dumping at N-th instruction");
    parser.addFlag('e', "M", "end dumping at M-th instruction");
    parser.addFlag('y', "*_symTab.yaml", "YAML symbol table file to show annotation");
    parser.addFlag('H', "omit the header information");
    trace_tools::addTracepointCommandLineArgs(parser, "-s", "-e");

    parser.addPositionalArgument("trace", "trace in STF format");

    parser.parseArguments(argc, argv);

    config.concise_mode = parser.hasArgument('c');
    config.user_mode_only = parser.hasArgument('u');
    stf::format_utils::setShowPhys(parser.hasArgument('p'));
    config.show_pte = parser.hasArgument('P');
    config.use_aliases = parser.hasArgument('A');
    config.match_symbol_opcode = parser.hasArgument('m');

    parser.getArgumentValue('s', config.start_inst);
    parser.getArgumentValue('e', config.end_inst);
    parser.getArgumentValue('y', config.symbol_filename);
    config.show_annotation = !config.symbol_filename.empty();
    config.omit_header = parser.hasArgument('H');

    trace_tools::getTracepointCommandLineArgs(parser,
                                              config.use_tracepoint_roi,
                                              config.roi_start_opcode,
                                              config.roi_stop_opcode,
                                              config.use_pc_roi,
                                              config.roi_start_pc,
                                              config.roi_stop_pc);

    parser.getPositionalArgument(0, config.trace_filename);

    stf_assert(!config.end_inst || (config.end_inst >= config.start_inst),
               "End inst (" << config.end_inst << ") must be greater than or equal to start inst (" << config.start_inst << ')');

    return config;
}

/**
 * Prints an opcode along with its disassembly
 * \param opcode instruction opcode
 * \param pc instruction PC
 * \param os The ostream to write the assembly to
 */
static inline void printOpcodeWithDisassembly(const stf::Disassembler& dis,
                                              const uint32_t opcode,
                                              const uint64_t pc) {
    static constexpr int OPCODE_PADDING = stf::format_utils::OPCODE_FIELD_WIDTH - stf::format_utils::OPCODE_WIDTH - 1;

    dis.printOpcode(std::cout, opcode);

    stf::print_utils::printSpaces(OPCODE_PADDING); // pad out the rest of the opcode field with spaces

    dis.printDisassembly(std::cout, pc, opcode);
    // if (show_annotation)
    // {
    //     // Retrieve symbol information from symbol table hash map
    //     symInfo = annoSt->getSymbol(dism_pc);
    //     if (match_symbol_opcode)
    //     {
    //         if(symInfo.opcode != inst.opcode())
    //             std::cout << " | " << " [ " << symInfo.libName << ", " << symInfo.symName << ", OPCODE_MISMATCH: " << std::hex  << symInfo.opcode << " ] ";
    //         else
    //             std::cout << " | " << " [ " << symInfo.libName << ", " << symInfo.symName << ", OPCODE_CROSSCHECKED"  << " ] ";
    //     }
    //     else
    //         std::cout << " | " << " [ " << symInfo.libName << ", " << symInfo.symName << " ] ";
    // }
    std::cout << std::endl;
}

template<typename IteratorType, typename StartStopType = std::nullopt_t>
void processTrace_(const STFDumpConfig& config, const StartStopType start_point = std::nullopt, const StartStopType stop_point = std::nullopt) {
    // Open stf trace reader
    stf::STFInstReader stf_reader(config.trace_filename, config.user_mode_only, stf::format_utils::showPhys());
    stf_reader.checkVersion();

    // Create disassembler
    stf::Disassembler dis(findElfFromTrace(config.trace_filename), stf_reader, config.use_aliases);

    if(!config.omit_header) {
        // Print Version info
        stf::print_utils::printLabel("VERSION");
        std::cout << stf_reader.major() << '.' << stf_reader.minor() << std::endl;

        // Print trace info
        for(const auto& i: stf_reader.getTraceInfo()) {
            std::cout << *i;
        }

        // Print Instruction set info
        stf::print_utils::printLabel("ISA");
        std::cout << stf_reader.getISA() << std::endl;

        stf::print_utils::printLabel("INST_IEM");
        std::cout << stf_reader.getInitialIEM() << std::endl;

        stf::print_utils::printLabel("INST_EXT");
        std::cout << stf_reader.getISAExtendedInfo() << std::endl;

        if(config.start_inst || config.end_inst) {
            std::cout << "Start Inst:" << config.start_inst;

            if(config.end_inst) {
                std::cout << "  End Inst:" << config.end_inst << std::endl;
            }
            else {
                std::cout << std::endl;
            }
        }
    }

    uint32_t hw_tid_prev = std::numeric_limits<uint32_t>::max();
    uint32_t pid_prev = std::numeric_limits<uint32_t>::max();
    uint32_t tid_prev = std::numeric_limits<uint32_t>::max();

    const auto start_inst = config.start_inst ? config.start_inst - 1 : 0;

    for (auto it = stf::getStartIterator<IteratorType>(stf_reader, start_inst, start_point, stop_point); it != stf_reader.end(); ++it) {
        const auto& inst = *it;

        if (STF_EXPECT_FALSE(!inst.valid())) {
            std::cerr << "ERROR: " << inst.index() << " invalid instruction " << std::hex << inst.opcode() << " PC " << inst.pc() << std::endl;
        }

        const uint32_t hw_tid = inst.hwtid();
        const uint32_t pid = inst.pid();
        const uint32_t tid = inst.tid();
        if (STF_EXPECT_FALSE(!config.concise_mode && (tid != tid_prev || pid != pid_prev || hw_tid != hw_tid_prev))) {
            stf::print_utils::printLabel("PID");
            stf::print_utils::printTID(hw_tid);
            std::cout << ':';
            stf::print_utils::printTID(pid);
            std::cout << ':';
            stf::print_utils::printTID(tid);
            std::cout << std::endl;
        }
        hw_tid_prev = hw_tid;
        pid_prev = pid;
        tid_prev = tid;

        // Opcode width string (INST32/INST16) and index should each take up half of the label column
        stf::print_utils::printLeft(inst.getOpcodeWidthStr(), stf::format_utils::LABEL_WIDTH / 2);

        stf::print_utils::printDecLeft(inst.index(), stf::format_utils::LABEL_WIDTH / 2);

        stf::print_utils::printVA(inst.pc());

        if (stf::format_utils::showPhys()) {
            // Make sure we zero-fill as needed, so that the address remains "virt:phys" and not "virt:  phys"
            std::cout << ':';
            stf::print_utils::printPA(inst.physPc());
        }
        stf::print_utils::printSpaces(1);

        if (STF_EXPECT_FALSE(inst.isTakenBranch())) {
            std::cout << "PC ";
            stf::print_utils::printVA(inst.branchTarget());
            if (stf::format_utils::showPhys()) {
                std::cout << ':';
                stf::print_utils::printPA(inst.physBranchTarget());
            }
            stf::print_utils::printSpaces(1);
        }
        else if(STF_EXPECT_FALSE(config.concise_mode && (inst.isFault() || inst.isInterrupt()))) {
            const std::string_view fault_msg = inst.isFault() ? "FAULT" : "INTERRUPT";
            stf::print_utils::printLeft(fault_msg, stf::format_utils::VA_WIDTH + 4);
            if (stf::format_utils::showPhys()) {
                stf::print_utils::printSpaces(stf::format_utils::PA_WIDTH + 1);
            }
        }
        else {
            stf::print_utils::printSpaces(stf::format_utils::VA_WIDTH + 4);
            if (stf::format_utils::showPhys()) {
                stf::print_utils::printSpaces(stf::format_utils::PA_WIDTH + 1);
            }
        }

        stf::print_utils::printSpaces(9); // Additional padding so that opcode lines up with operand values
        printOpcodeWithDisassembly(dis, inst.opcode(), inst.pc());

        if(!config.concise_mode) {
            for(const auto& m: inst.getMemoryAccesses()) {
                std::cout << m << std::endl;
            }

            if (config.show_pte) {
                for(const auto& pte: inst.getEmbeddedPTEs()) {
                    std::cout << pte->template as<stf::PageTableWalkRecord>();
                }
            }

            for(const auto& reg: inst.getRegisterStates()) {
                std::cout << reg << std::endl;
            }

            for(const auto& reg: inst.getOperands()) {
                std::cout << reg << std::endl;
            }

            for(const auto& evt: inst.getEvents()) {
                std::cout << evt << std::endl;
            }

            for(const auto& cmt: inst.getComments()) {
                std::cout << cmt->template as<stf::CommentRecord>() << std::endl;
            }

            for(const auto& uop: inst.getMicroOps()) {
                const auto& microop = uop->template as<stf::InstMicroOpRecord>();
                stf::print_utils::printOperandLabel(microop.getSize() == 2 ? "UOp16 " : "UOp32 ");
                stf::print_utils::printSpaces(stf::format_utils::REGISTER_NAME_WIDTH + stf::format_utils::DATA_WIDTH);
                printOpcodeWithDisassembly(dis, microop.getMicroOp(), inst.pc());
            }

            for(const auto& reg: inst.getReadyRegs()) {
                stf::print_utils::printOperandLabel("ReadyReg ");
                std::cout << std::dec << reg->template as<stf::InstReadyRegRecord>().getReg() << std::endl;
            }
        }

        if (STF_EXPECT_FALSE(config.end_inst && (inst.index() >= config.end_inst))) {
            break;
        }
    }
}

int main (int argc, char **argv)
{
    // Get arguments
    try {
        const STFDumpConfig config = parseCommandLine (argc, argv);

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
    catch(const trace_tools::CommandLineParser::EarlyExitException& e) {
        std::cerr << e.what() << std::endl;
        return e.getCode();
    }

    return 0;
}
