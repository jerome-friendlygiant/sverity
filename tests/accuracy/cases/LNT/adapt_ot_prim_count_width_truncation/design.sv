// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from hw/ip/prim/rtl/prim_count.sv. @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [3:0] count_i,
  input  logic       error_i,
  output logic [3:0] status_o
);
  logic [4:0] status_full;
  always_comb begin
    status_full = {error_i, count_i};
    status_o = status_full;
  end
endmodule
