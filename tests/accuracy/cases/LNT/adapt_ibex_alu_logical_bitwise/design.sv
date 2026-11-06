// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from rtl/ibex_alu.sv. @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [1:0]  operator_i,
  input  logic [31:0] operand_a_i,
  input  logic [31:0] operand_b_i,
  output logic [31:0] result_o
);
  always_comb begin
    case (operator_i)
      2'd0: result_o = operand_a_i && operand_b_i;
      2'd1: result_o = operand_a_i | operand_b_i;
      default: result_o = 32'h0;
    endcase
  end
endmodule
