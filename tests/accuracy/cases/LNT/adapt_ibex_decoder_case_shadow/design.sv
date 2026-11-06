// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Copyright Microsoft Corporation
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from rtl/ibex_decoder.sv. @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [6:0] opcode_i,
  output logic       is_alu_o,
  output logic       is_load_o
);
  always_comb begin
    is_alu_o = 1'b0;
    is_load_o = 1'b0;
    casez (opcode_i)
      7'b?????11: is_alu_o = 1'b1;
      7'b0000011: is_load_o = 1'b1;
      7'b0110011: is_alu_o = 1'b1;
      default: ;
    endcase
  end
endmodule
