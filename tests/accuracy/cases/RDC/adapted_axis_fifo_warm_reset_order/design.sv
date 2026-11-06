// Adapted from verilog-axis rtl/axis_fifo.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce.
// Copyright (c) 2013-2023 Alex Forencich
// Licensed under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
    input  logic        clk_wr,
    input  logic        clk_rd,
    input  logic        rst_wr_ni,
    input  logic        rst_rd_ni,
    input  logic        wr_valid_i,
    input  logic [7:0]  wr_data_i,
    input  logic        rd_ready_i,
    output logic        rd_valid_o,
    output logic [7:0]  rd_data_o
);
    logic [7:0] mem_q;
    logic wr_seen_q, rd_seen_q;
    logic wr_seen_sync_q, rd_seen_sync_q;

    always_ff @(posedge clk_wr or negedge rst_wr_ni) begin
        if (!rst_wr_ni) begin
            wr_seen_q      <= 1'b0;
            rd_seen_sync_q <= 1'b0;
        end else begin
            rd_seen_sync_q <= rd_seen_q;
            if (wr_valid_i && !wr_seen_q) begin
                mem_q     <= wr_data_i;
                wr_seen_q <= 1'b1;
            end
        end
    end

    always_ff @(posedge clk_rd or negedge rst_rd_ni) begin
        if (!rst_rd_ni) begin
            rd_seen_q      <= 1'b0;
            wr_seen_sync_q <= 1'b0;
            rd_valid_o     <= 1'b0;
            rd_data_o      <= '0;
        end else begin
            wr_seen_sync_q <= wr_seen_q;
            rd_valid_o     <= wr_seen_sync_q ^ rd_seen_q;
            if (wr_seen_sync_q ^ rd_seen_q)
                rd_data_o <= mem_q;
            if (rd_ready_i && (wr_seen_sync_q ^ rd_seen_q))
                rd_seen_q <= wr_seen_sync_q;
        end
    end
endmodule
