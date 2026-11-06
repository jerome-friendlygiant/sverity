// Adapted from opentitan hw/ip/prim_generic/rtl/prim_flop.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic       clk,
    input  logic       ext_rst_ni,
    input  logic [7:0] din,
    output logic [7:0] dout
);
    always_ff @(posedge clk or negedge ext_rst_ni) begin
        if (!ext_rst_ni)
            dout <= '0;
        else
            dout <= din;
    end
endmodule
