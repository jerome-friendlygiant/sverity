// Status bit from the slow block reported at the fast-clock boundary.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic dst_rst_ni,
  input  logic err_in_i,
  output logic err_o
);
  logic src_stat_q;
  logic s1_q;
  logic s2_q;
  logic out_q;

  always_ff @(posedge src_clk) src_stat_q <= err_in_i;

  always_ff @(posedge dst_clk) begin
    s1_q <= src_stat_q;
    s2_q <= s1_q;
  end

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) out_q <= 1'b0;
    else             out_q <= s2_q;
  end

  assign err_o = out_q;
endmodule
