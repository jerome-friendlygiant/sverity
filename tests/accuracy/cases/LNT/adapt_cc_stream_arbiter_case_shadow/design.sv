// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.
// SPDX-License-Identifier: SHL-0.51
// Adapted from src/cc_stream_arbiter.sv. @e73baaec2ca665cd80c3c384e9258e35242b829c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [1:0] request_i,
  output logic [1:0] grant_o
);
  always_comb begin
    grant_o = 2'b00;
    casez (request_i)
      2'b?1: grant_o = 2'b01;
      2'b01: grant_o = 2'b10;
      2'b10: grant_o = 2'b01;
      default: grant_o = 2'b00;
    endcase
  end
endmodule
