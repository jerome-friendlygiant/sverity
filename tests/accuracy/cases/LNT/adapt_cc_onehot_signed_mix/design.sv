// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.
// SPDX-License-Identifier: SHL-0.51
// Adapted from src/cc_onehot_to_bin.sv. @e73baaec2ca665cd80c3c384e9258e35242b829c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic signed [3:0] index_i,
  input  logic        [3:0] limit_i,
  output logic              below_o
);
  always_comb begin
    below_o = index_i < limit_i;
  end
endmodule
