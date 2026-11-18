// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
import "DPI-C" function void calc_gain(input int unsigned code, output int unsigned gain);

module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic [7:0]  code_i,
  input  logic        apply_i,
  output logic [31:0] gain_o
);
  int unsigned gain_w;
  logic [31:0] gain_q;

  always_comb begin
    calc_gain(32'(code_i), gain_w);
  end

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)       gain_q <= '0;
    else if (apply_i)  gain_q <= gain_w;
  end

  assign gain_o = gain_q;
endmodule
