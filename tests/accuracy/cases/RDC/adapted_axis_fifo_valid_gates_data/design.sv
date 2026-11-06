// Adapted from verilog-axis rtl/axis_fifo.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce.
// Copyright (c) 2013-2023 Alex Forencich
// Licensed under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
    input  logic       clk,
    input  logic       rst,
    input  logic       in_valid,
    input  logic [15:0] in_data,
    output logic       in_ready,
    output logic       out_valid,
    input  logic       out_ready,
    output logic [15:0] out_data
);
    logic [15:0] data_q;

    assign in_ready = !out_valid || out_ready;
    assign out_data = data_q;

    always_ff @(posedge clk) begin
        if (rst) begin
            out_valid <= 1'b0;
        end else if (in_ready) begin
            out_valid <= in_valid;
            if (in_valid)
                data_q <= in_data;
        end
    end
endmodule
