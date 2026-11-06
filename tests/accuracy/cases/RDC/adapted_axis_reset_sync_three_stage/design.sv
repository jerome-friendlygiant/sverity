// Adapted from verilog-axis rtl/sync_reset.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce.
// Copyright (c) 2014-2020 Alex Forencich
// Licensed under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
    input  logic       clk,
    input  logic       arst,
    input  logic [7:0] din,
    input  logic       din_valid,
    output logic [7:0] dout,
    output logic       dout_valid
);
    logic [2:0] rst_pipe;

    always_ff @(posedge clk or posedge arst) begin
        if (arst)
            rst_pipe <= 3'b111;
        else
            rst_pipe <= {rst_pipe[1:0], 1'b0};
    end

    always_ff @(posedge clk or posedge arst) begin
        if (arst) begin
            dout_valid <= 1'b0;
            dout       <= '0;
        end else if (!rst_pipe[2]) begin
            dout_valid <= din_valid;
            if (din_valid)
                dout <= din;
        end else begin
            dout_valid <= 1'b0;
        end
    end
endmodule
