// Divided-clock capture block.
module top (
  input  logic clk_sys,
  input  logic d_i,
  output logic q_o
);
  logic div2_clk_q;

  always_ff @(posedge clk_sys) div2_clk_q <= ~div2_clk_q;
  always_ff @(posedge div2_clk_q) q_o <= d_i;
endmodule
