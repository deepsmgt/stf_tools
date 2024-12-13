#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "command_line_parser.hpp"

#include "stf_decoder.hpp"
#include "stf_inst_reader.hpp"
#include "stf_reg_state.hpp"
#include "stf_writer.hpp"
#include "stf_record_types.hpp"

class STFMorpher {
    private:
        class OpcodeMorph {
            public:
                class Op {
                    private:
                        uint32_t opcode_;
                        mutable std::vector<stf::InstRegRecord> operands_;
                        mutable uint64_t ls_address_;
                        int64_t ls_stride_;
                        uint16_t ls_size_;
                        stf::INST_MEM_ACCESS ls_access_type_;
                        size_t op_size_;

                    public:
                        Op(const uint32_t opcode,
                           std::vector<stf::InstRegRecord>&& operands,
                           const uint64_t ls_address,
                           const int64_t ls_stride,
                           const uint16_t ls_size,
                           const stf::INST_MEM_ACCESS ls_access_type,
                           const size_t op_size) :
                            opcode_(opcode),
                            operands_(std::move(operands)),
                            ls_address_(ls_address),
                            ls_stride_(ls_stride),
                            ls_size_(ls_size),
                            ls_access_type_(ls_access_type),
                            op_size_(op_size)
                        {
                        }

                        size_t write(stf::STFWriter& writer, const stf::STFRegState& reg_state) const;
                };

            private:
                size_t total_size_ = 0;
                std::vector<Op> opcodes_;

            public:
                inline void addOp(const uint32_t opcode,
                                  std::vector<stf::InstRegRecord>&& operands,
                                  const uint64_t ls_address,
                                  const int64_t ls_stride,
                                  const uint16_t ls_size,
                                  const stf::INST_MEM_ACCESS ls_access_type,
                                  const size_t op_size) {
                    opcodes_.emplace_back(opcode,
                                          std::forward<std::vector<stf::InstRegRecord>>(operands),
                                          ls_address,
                                          ls_stride,
                                          ls_size,
                                          ls_access_type,
                                          op_size);
                    total_size_ += op_size;
                }

                const auto& getOpcodes() const {
                    return opcodes_;
                }

                auto getTotalSize() const {
                    return total_size_;
                }
        };

        enum class MorphType : uint8_t {
            STFID = 0,
            PC = 1,
            NUM_TYPES = 2
        };
        using MorphInt = stf::enums::int_t<MorphType>;
        using MorphMap = std::unordered_map<uint64_t, OpcodeMorph>;

        std::array<MorphMap, stf::enums::to_int(MorphType::NUM_TYPES)> morphs_;
        stf::STFInstReader reader_;
        stf::STFWriter writer_;
        stf::STFRegState reg_state_;
        stf::STFInstReader::iterator it_;
        const bool allow_collisions_;
        const uint64_t end_inst_;
        stf::STFDecoder decoder_;

        template<MorphType morph_type>
        static inline constexpr char getArgumentFlag_() {
            static_assert(morph_type != MorphType::NUM_TYPES, "Invalid morph type");

            switch(morph_type) {
                case MorphType::STFID:
                    return 'i';
                case MorphType::PC:
                    return 'a';
            }
        }

        static inline char getArgumentFlag_(const MorphType morph_type) {
            switch(morph_type) {
                case MorphType::STFID:
                    return getArgumentFlag_<MorphType::STFID>();
                case MorphType::PC:
                    return getArgumentFlag_<MorphType::PC>();
                case MorphType::NUM_TYPES:
                    break;
            }

            stf_throw("Invalid morph type");
        }

        static inline char getArgumentFlag_(const MorphInt morph_type) {
            return getArgumentFlag_(static_cast<MorphType>(morph_type));
        }

        static inline uint64_t convertMorphId_(const MorphType morph_type, const std::string& id_str) {
            switch(morph_type) {
                case MorphType::STFID:
                    return std::stoull(id_str);
                case MorphType::PC:
                    return std::stoull(id_str, 0, 16);
                case MorphType::NUM_TYPES:
                    break;
            }

            stf_throw("Invalid morph type");
        }

        static inline uint64_t convertMorphId_(const MorphInt morph_type, const std::string& id_str) {
            return convertMorphId_(static_cast<MorphType>(morph_type), id_str);
        }

        static inline std::string formatMorphIndex_(const MorphType morph_type, const uint64_t index) {
            std::stringstream ss;

            switch(morph_type) {
                case MorphType::STFID:
                    ss << "STFID(" << index << ")";
                    break;
                case MorphType::PC:
                    ss << "PC(" << std::hex << index << ")";
                    break;
                case MorphType::NUM_TYPES:
                    stf_throw("Invalid morph type");
            }

            return ss.str();
        }

        static inline std::string formatMorphIndex_(const MorphInt morph_type, const uint64_t index) {
            return formatMorphIndex_(static_cast<MorphType>(morph_type), index);
        }

        inline uint64_t getMorphIndex_(const MorphType morph_type) {
            switch(morph_type) {
                case MorphType::STFID:
                    return it_->index();
                case MorphType::PC:
                    return it_->pc();
                case MorphType::NUM_TYPES:
                    break;
            }

            stf_throw("Invalid morph type");
        }

        inline uint64_t getMorphIndex_(const MorphInt morph_type) {
            return getMorphIndex_(static_cast<MorphType>(morph_type));
        }

        void updateInitialRegState_() {
            for(const auto& op: it_->getRegisterStates()) {
                reg_state_.regStateUpdate(op.getRecord());
            }

            for(const auto& op: it_->getSourceOperands()) {
                reg_state_.regStateUpdate(op.getRecord());
            }
        }

        void updateFinalRegState_() {
            for(const auto& op: it_->getDestOperands()) {
                reg_state_.regStateUpdate(op.getRecord());
            }
        }

        void processOpcodeMorphArguments_(const trace_tools::CommandLineParser& parser);

    public:
        static inline void addMorphArguments(trace_tools::CommandLineParser& parser) {
            parser.addFlag('A', "address", "assume all LS ops access the given address");
            parser.addFlag('S', "size", "assume all LS ops have the given size");
            parser.addFlag("stride", "stride", "increment all LS ops' addresses by the given stride after each instance");
            parser.addFlag('C', "allow STFID and PC-based morphs to collide. STFID morphs will take precedence.");
            parser.addMultiFlag(getArgumentFlag_<MorphType::PC>(),
                                "pc=opcode1[@addr1:size1[+stride1]][,opcode2[@addr2:size2[+stride2]],...]",
                                "morph instruction(s) starting at pc to specified opcode(s). "
                                "LS instructions can have target addresses and access sizes (and an optional stride) "
                                "specified with `opcode@addr:size+stride` syntax");
            parser.addMultiFlag(getArgumentFlag_<MorphType::STFID>(),
                                "stfid=opcode1[@addr1:size1[+stride1]][,opcode2[@addr2:size2[+stride2]],...]",
                                "morph instruction(s) starting at stfid to specified opcode(s). "
                                "LS instructions can have target addresses and access sizes (and an optional stride) "
                                "specified with `opcode@addr:size+stride` syntax");
        }

        inline bool empty() const {
            return std::all_of(morphs_.begin(), morphs_.end(), [](const auto& morphs) { return morphs.empty(); });
        }

        void process();

        STFMorpher(const trace_tools::CommandLineParser& parser,
                   const std::string& trace,
                   const std::string& output,
                   const uint64_t start_inst,
                   const uint64_t end_inst) :
            reader_(trace),
            writer_(output),
            reg_state_(reader_.getISA(), reader_.getInitialIEM()),
            it_(start_inst > 1 ? reader_.seekFromBeginning(start_inst - 1) : reader_.begin()),
            allow_collisions_(parser.hasArgument('C')),
            end_inst_(end_inst),
            decoder_(reader_.getInitialIEM())
        {
            processOpcodeMorphArguments_(parser);
        }

        STFMorpher(STFMorpher&& rhs) = default;
};
