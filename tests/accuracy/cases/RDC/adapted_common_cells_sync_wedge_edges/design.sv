// Adapted from common_cells src/cc_sync_wedge.sv @e73baaec2ca665cd80c3c384e9258e35242b829c.
// Copyright 2018 ETH Zurich and University of Bologna.
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License").
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk,
    input  logic rst_ni,
    input  logic serial_i,
    output logic rising_o,
    output logic falling_o,
    output logic serial_o
);
    logic sync_1_q, sync_2_q, previous_q;

    always_ff @(posedge clk or negedge rst_ni) begin
        if (!rst_ni) begin
            sync_1_q  <= 1'b0;
            sync_2_q  <= 1'b0;
            previous_q <= 1'b0;
        end else begin
            sync_1_q  <= serial_i;
            sync_2_q  <= sync_1_q;
            previous_q <= sync_2_q;
        end
    end

    assign serial_o = sync_2_q;
    assign rising_o = sync_2_q & ~previous_q;
    assign falling_o = ~sync_2_q & previous_q;
endmodule
