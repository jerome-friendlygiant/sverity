// Adapted from ibex rtl/ibex_fetch_fifo.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca.
// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic        clk,
    input  logic        rst_ni,
    input  logic        clear_i,
    input  logic        in_valid_i,
    input  logic [31:0] in_rdata_i,
    input  logic        out_ready_i,
    output logic        out_valid_o,
    output logic [31:0] out_rdata_o
);
    logic [31:0] rdata_q;

    assign out_rdata_o = rdata_q;

    always_ff @(posedge clk or negedge rst_ni) begin
        if (!rst_ni) begin
            out_valid_o <= 1'b0;
        end else if (clear_i) begin
            out_valid_o <= 1'b0;
        end else if (!out_valid_o || out_ready_i) begin
            out_valid_o <= in_valid_i;
            if (in_valid_i)
                rdata_q <= in_rdata_i;
        end
    end
endmodule
