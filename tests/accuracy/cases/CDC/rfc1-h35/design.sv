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
  input  logic clk_a,
  input  logic clk_b,
  input  logic dst_rst_ni,
  input  logic d_i,
  output logic q_o
);
  logic src_q;
  logic sync_s;
  logic out_q;

  always_ff @(posedge clk_a) src_q <= d_i;

  sync2 u_sync (.clk(clk_b), .d_i(src_q), .q_o(sync_s));

  always_ff @(posedge clk_b) begin
    if (!dst_rst_ni) out_q <= 1'b0;
    else             out_q <= sync_s;
  end

  assign q_o = out_q;
endmodule
