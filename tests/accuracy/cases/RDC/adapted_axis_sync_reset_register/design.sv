// Adapted from verilog-axis rtl/axis_register.v @48ff7a7e2ef782cf778d47910cf85835c64b1bce.
// Copyright (c) 2014-2018 Alex Forencich
// Licensed under the MIT License.
// Reduced and modified for the SVerity golden set.

module top (
    input  logic        clk,
    input  logic        rst,
    input  logic        s_valid,
    input  logic [7:0]  s_data,
    output logic        s_ready,
    output logic        m_valid,
    output logic [7:0]  m_data,
    input  logic        m_ready
);
    assign s_ready = !m_valid || m_ready;

    always_ff @(posedge clk) begin
        if (rst) begin
            m_valid <= 1'b0;
            m_data  <= '0;
        end else if (s_ready) begin
            m_valid <= s_valid;
            if (s_valid)
                m_data <= s_data;
        end
    end
endmodule
