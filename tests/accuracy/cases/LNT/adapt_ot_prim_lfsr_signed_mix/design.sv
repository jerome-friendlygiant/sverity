// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from hw/ip/prim/rtl/prim_lfsr.sv. @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic signed [7:0] seed_i,
  input  logic        [7:0] state_i,
  output logic              reseed_o
);
  always_comb begin
    reseed_o = seed_i < state_i;
  end
endmodule
