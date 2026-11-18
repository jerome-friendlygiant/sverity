// sverity-args: --top top
// A buffer cell built from two inverters drives the reset (the shape of a buffer cell kept apart from optimisation): the pair is a copy.
// expect-none: LNT-047
module buf_cell(input logic in_i, output logic out_o);
  logic inv;
  assign inv = ~in_i;
  assign out_o = ~inv;
endmodule
module top(input logic clk, rst_ni, d, output logic q);
  logic rst_n;
  buf_cell u_buf(.in_i(rst_ni), .out_o(rst_n));
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
