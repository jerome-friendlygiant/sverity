// Adapted from lowRISC/ibex rtl/ibex_branch_predict.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic [31:0] instr_i,
  input  logic [31:0] pc_i,
  input  logic        valid_i,
  output logic        taken_o,
  output logic [31:0] target_o
);
  logic [31:0] imm_j, imm_b, imm_cj, imm_cb, branch_imm;
  logic instr_j, instr_b, instr_cj, instr_cb;

  assign imm_j  = {{12{instr_i[31]}}, instr_i[19:12], instr_i[20], instr_i[30:21], 1'b0};
  assign imm_b  = {{19{instr_i[31]}}, instr_i[31], instr_i[7], instr_i[30:25], instr_i[11:8], 1'b0};
  assign imm_cj = {{20{instr_i[12]}}, instr_i[12], instr_i[8], instr_i[10:9], instr_i[6],
                   instr_i[7], instr_i[2], instr_i[11], instr_i[5:3], 1'b0};
  assign imm_cb = {{23{instr_i[12]}}, instr_i[12], instr_i[6:5], instr_i[2], instr_i[11:10],
                   instr_i[4:3], 1'b0};

  assign instr_b = instr_i[6:0] == 7'b1100011;
  assign instr_j = instr_i[6:0] == 7'b1101111;
  assign instr_cb = (instr_i[1:0] == 2'b01) && (instr_i[15:13] inside {3'b110, 3'b111});
  assign instr_cj = (instr_i[1:0] == 2'b01) && (instr_i[15:13] inside {3'b101, 3'b001});

  always_comb begin
    branch_imm = imm_b;
    unique case (1'b1)
      instr_j:  branch_imm = imm_j;
      instr_b:  branch_imm = imm_b;
      instr_cj: branch_imm = imm_cj;
      instr_cb: branch_imm = imm_cb;
      default:  ;
    endcase
  end

  assign taken_o = valid_i && (instr_j || instr_cj ||
                               (instr_b && imm_b[31]) || (instr_cb && imm_cb[31]));
  assign target_o = pc_i + branch_imm;
endmodule
