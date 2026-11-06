// Adapted from opentitan hw/ip/i3c/rtl/i3c_target_rst.sv @92f3d03b0dcf6d5c1933a65d1497747f67d19f4c.
// Copyright lowRISC contributors (OpenTitan project).
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic enable_i,
    input  logic sw_reset_i,
    input  logic recovery_i,
    input  logic bus_avail_i,
    output logic reset_trig_o,
    output logic inbuf_en_o
);
    logic resetting_q;

    always_ff @(posedge clk_i or negedge rst_ni) begin
        if (!rst_ni)
            resetting_q <= 1'b1;
        else if (!enable_i || sw_reset_i || recovery_i)
            resetting_q <= 1'b1;
        else if (bus_avail_i)
            resetting_q <= 1'b0;
    end

    assign reset_trig_o = resetting_q;
    assign inbuf_en_o   = enable_i && !resetting_q;
endmodule
