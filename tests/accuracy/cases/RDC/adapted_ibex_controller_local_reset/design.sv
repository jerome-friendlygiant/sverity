// Adapted from ibex rtl/ibex_controller.sv @2373e5dadcfc4f56672962fa242f6a051acc9aca.
// Copyright lowRISC contributors.
// Copyright 2018 ETH Zurich and University of Bologna, see also CREDITS.md.
// Copyright Microsoft Corporation
// Licensed under the Apache License, Version 2.0 (the "License").
// SPDX-License-Identifier: Apache-2.0
// Reduced and modified for the SVerity golden set.

module top (
    input  logic clk_i,
    input  logic rst_ni,
    input  logic start_i,
    input  logic done_i,
    output logic busy_o
);
    typedef enum logic [1:0] {IDLE, RUN, WAIT_DONE} state_e;
    state_e state_q;

    always_ff @(posedge clk_i) begin
        if (!rst_ni)
            state_q <= IDLE;
        else begin
            case (state_q)
                IDLE:      if (start_i) state_q <= RUN;
                RUN:       state_q <= WAIT_DONE;
                WAIT_DONE: if (done_i) state_q <= IDLE;
                default:   state_q <= IDLE;
            endcase
        end
    end

    assign busy_o = (state_q != IDLE);
endmodule
