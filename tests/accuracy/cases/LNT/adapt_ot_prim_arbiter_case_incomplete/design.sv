// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0, see LICENSE for details.
// SPDX-License-Identifier: Apache-2.0
// Adapted from hw/ip/prim/rtl/prim_arbiter_tree.sv. @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
// Reduced and modified for the SVerity golden set.
module top (
  input  logic [2:0] req_i,
  output logic [2:0] grant_o
);
  always_comb begin
    casez (req_i)
      3'b1??: grant_o = 3'b100;
      3'b01?: grant_o = 3'b010;
      3'b001: grant_o = 3'b001;
    endcase
  end
endmodule
