// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic        clk_i,
  input  logic        rst_ni,
  input  logic        clear_i,
  input  logic        valid_i,
  input  logic [15:0] a_i,
  input  logic [15:0] b_i,
  output logic [31:0] acc_o,
  output logic        acc_nonzero_o
);
  logic [31:0] prod;
  logic [31:0] acc_q;

  vendor_mult16 u_mult (
    .a (a_i),
    .b (b_i),
    .p (prod)
  );

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni)       acc_q <= '0;
    else if (clear_i)  acc_q <= '0;
    else if (valid_i)  acc_q <= acc_q + prod;
  end

  assign acc_o         = acc_q;
  assign acc_nonzero_o = |acc_q;
endmodule
