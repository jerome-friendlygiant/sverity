// Adapted from common_cells src/cc_rstgen_bypass.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set.

module top (
    input  logic       clk_i,
    input  logic       functional_rst_ni,
    input  logic       scan_rst_ni,
    input  logic       test_mode_i,
    input  logic [7:0] din_i,
    output logic [7:0] dout_o
);
    logic selected_rst_ni;

    assign selected_rst_ni = test_mode_i ? scan_rst_ni : functional_rst_ni;

    always_ff @(posedge clk_i or negedge selected_rst_ni) begin
        if (!selected_rst_ni)
            dout_o <= '0;
        else
            dout_o <= din_i;
    end
endmodule
