module sync2 (
  input  logic clk,
  input  logic d_i,
  output logic q_o
);
  logic s1_q;
  logic s2_q;

  always_ff @(posedge clk) begin
    s1_q <= d_i;
    s2_q <= s1_q;
  end

  assign q_o = s2_q;
endmodule

module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic a_i,
  input  logic b_i,
  output logic comb_o
);
  logic a_src_q;
  logic b_src_q;
  logic a_s;
  logic b_s;

  always_ff @(posedge src_clk) begin
    a_src_q <= a_i;
    b_src_q <= b_i;
  end

  sync2 u_sync_a (.clk(dst_clk), .d_i(a_src_q), .q_o(a_s));
  sync2 u_sync_b (.clk(dst_clk), .d_i(b_src_q), .q_o(b_s));

  assign comb_o = a_s ^ b_s;
endmodule
