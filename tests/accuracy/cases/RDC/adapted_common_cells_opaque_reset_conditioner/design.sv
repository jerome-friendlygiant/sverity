// Adapted from common_cells src/cc_rstgen.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set.

module reset_conditioner (
    input  logic clk_i,
    input  logic ext_rst_ni,
    output logic rst_no
);
endmodule

module top (
    input  logic       clk_i,
    input  logic       ext_rst_ni,
    input  logic [7:0] din_i,
    output logic [7:0] dout_o
);
    logic conditioned_rst_no;

    reset_conditioner u_conditioner (
        .clk_i(clk_i),
        .ext_rst_ni(ext_rst_ni),
        .rst_no(conditioned_rst_no)
    );

    always_ff @(posedge clk_i or posedge conditioned_rst_no) begin
        if (conditioned_rst_no)
            dout_o <= '0;
        else
            dout_o <= din_i;
    end
endmodule
