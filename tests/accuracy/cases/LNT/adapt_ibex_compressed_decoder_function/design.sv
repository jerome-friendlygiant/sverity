// Adapted from lowRISC/ibex rtl/ibex_compressed_decoder.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Copyright Microsoft Corporation
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
  input  logic [3:0] rlist_i,
  input  logic [1:0] spimm_i,
  input  logic       decrement_i,
  output logic [31:0] instr_o
);
  function automatic logic [6:0] stack_adjust(input logic [3:0] rlist);
    unique case (rlist)
      4'd4, 4'd5, 4'd6, 4'd7:   return 7'd16;
      4'd8, 4'd9, 4'd10, 4'd11: return 7'd32;
      4'd12, 4'd13, 4'd14:      return 7'd48;
      4'd15:                    return 7'd64;
      default:                  return 7'd0;
    endcase
  endfunction

  function automatic logic [31:0] make_addi(
      input logic [3:0] rlist, input logic [1:0] spimm, input logic decrement);
    logic [11:0] imm;
    logic signed [11:0] signed_imm;
    imm = {5'b0, stack_adjust(rlist)} + {10'b0, spimm, 2'b00};
    signed_imm = decrement ? -$signed(imm) : $signed(imm);
    make_addi = {unsigned'(signed_imm), 5'd2, 3'b000, 5'd2, 7'b0010011};
  endfunction

  assign instr_o = make_addi(rlist_i, spimm_i, decrement_i);
endmodule
