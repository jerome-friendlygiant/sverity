// Status bit with a destination-side enable.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic d_i,
  input  logic en_dst_i,
  output logic q_o
);
  logic src_q;
  logic s1_q;
  logic s2_q;
  logic out_q;

  always_ff @(posedge clk_a) src_q <= d_i;

  always_ff @(posedge clk_b) begin
    s1_q <= src_q;
    s2_q <= s1_q & en_dst_i;
  end

  always_ff @(posedge clk_b) out_q <= s2_q;

  assign q_o = out_q;
endmodule
