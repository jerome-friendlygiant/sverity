// Status bit with a fast-path observation.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic d_i,
  input  logic mode_i,
  output logic early_o,
  output logic fast_o,
  output logic q_o
);
  logic src_q;
  logic s1_q;
  logic s2_q;
  logic fast_q;

  always_ff @(posedge clk_a) src_q <= d_i;

  always_ff @(posedge clk_b) begin
    s1_q <= src_q;
    s2_q <= s1_q;
  end

  always_ff @(posedge clk_b) fast_q <= s1_q & mode_i;

  assign early_o = s1_q;
  assign fast_o  = fast_q;
  assign q_o     = s2_q;
endmodule
