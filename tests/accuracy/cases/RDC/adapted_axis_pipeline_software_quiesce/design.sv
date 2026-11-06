// Adapted from verilog-axis rtl/axis_pipeline_register.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce.
// Copyright (c) 2018 Alex Forencich
// Licensed under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
    input  logic        clk_src,
    input  logic        clk_dst,
    input  logic        rst_src_ni,
    input  logic        rst_dst_ni,
    input  logic        src_valid_i,
    input  logic [7:0]  src_data_i,
    output logic        dst_valid_o,
    output logic [7:0]  dst_data_o
);
    logic src_valid_q;
    logic [7:0] src_data_q;
    logic valid_sync_1_q, valid_sync_2_q;

    always_ff @(posedge clk_src or negedge rst_src_ni) begin
        if (!rst_src_ni) begin
            src_valid_q <= 1'b0;
            src_data_q  <= '0;
        end else begin
            src_valid_q <= src_valid_i;
            if (src_valid_i)
                src_data_q <= src_data_i;
        end
    end

    always_ff @(posedge clk_dst or negedge rst_dst_ni) begin
        if (!rst_dst_ni) begin
            valid_sync_1_q <= 1'b0;
            valid_sync_2_q <= 1'b0;
            dst_valid_o    <= 1'b0;
            dst_data_o     <= '0;
        end else begin
            valid_sync_1_q <= src_valid_q;
            valid_sync_2_q <= valid_sync_1_q;
            dst_valid_o    <= valid_sync_2_q;
            if (valid_sync_2_q)
                dst_data_o <= src_data_q;
        end
    end
endmodule
