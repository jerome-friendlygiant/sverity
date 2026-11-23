// Capture register on a selectable clock.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic sel_i,
  input  logic d_i,
  output logic q_o
);
  logic clk_mux;
  logic q_q;

  assign clk_mux = sel_i ? clk_b : clk_a;

  always_ff @(posedge clk_mux) q_q <= d_i;

  assign q_o = q_q;
endmodule
