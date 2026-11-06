// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Copyright Microsoft Corporation
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from rtl/ibex_compressed_decoder.sv. @2373e5dadcfc4f56672962fa242f6a051acc9aca
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [15:0] instr_i,
  input  logic        valid_i,
  output logic [31:0] instr_o,
  output logic        illegal_o
);
  always_comb begin
    illegal_o = 1'b0;
    if (valid_i && instr_i[1:0] == 2'b00)
      instr_o = {16'h0000, instr_i};
    else if (valid_i && instr_i[1:0] == 2'b01)
      instr_o = {instr_i, 16'h0000};
  end
endmodule
