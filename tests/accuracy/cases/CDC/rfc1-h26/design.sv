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
  input  logic dst_rst_ni,
  input  logic ctrl_in_i,
  output logic ctrl_o
);
  logic src_ctrl_q;
  logic sync_s;
  logic shadow_a_q;
  logic shadow_b_q;
  logic comb_q;

  always_ff @(posedge src_clk) src_ctrl_q <= ctrl_in_i;

  sync2 u_sync (.clk(dst_clk), .d_i(src_ctrl_q), .q_o(sync_s));

  always_ff @(posedge dst_clk) begin
    shadow_a_q <= sync_s;
    shadow_b_q <= ~sync_s;
  end

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) comb_q <= 1'b0;
    else             comb_q <= shadow_a_q | shadow_b_q;
  end

  assign ctrl_o = comb_q;
endmodule
