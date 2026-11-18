// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top (
  input  logic [5:0] opcode_i,
  input  logic       funct_alt_i,
  output logic       is_alu_o,
  output logic       is_mem_o,
  output logic       is_coproc_o,
  output logic       is_illegal_o,
  output logic       sub_select_o
);

  typedef enum logic [2:0] {
    CL_ALU_RR,
    CL_ALU_IMM,
    CL_MEM,
    CL_COPROC,
    CL_ILLEGAL
  } op_class_e;

  op_class_e op_class;

  always_comb begin
    casez (opcode_i)
      6'b000???: op_class = CL_ALU_RR;
      6'b001???: op_class = CL_ALU_IMM;
      6'b01????: op_class = CL_MEM;
      6'b0110??: op_class = CL_COPROC;
      6'b1?????: op_class = CL_ILLEGAL;
      default:   op_class = CL_ILLEGAL;
    endcase
  end

  assign is_alu_o     = (op_class == CL_ALU_RR) || (op_class == CL_ALU_IMM);
  assign is_mem_o     = (op_class == CL_MEM);
  assign is_coproc_o  = (op_class == CL_COPROC);
  assign is_illegal_o = (op_class == CL_ILLEGAL);
  assign sub_select_o = funct_alt_i & (op_class == CL_ALU_RR);

endmodule
