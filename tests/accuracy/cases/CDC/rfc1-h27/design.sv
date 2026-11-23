// Optional pulse input recorded in a sticky seen flag.
module top (
  input  logic src_clk,
  input  logic dst_clk,
  input  logic dst_rst_ni,
  input  logic pulse_i,
  output logic seen_o
);
  logic p1_q;
  logic p2_q;
  logic seen_q;

  always_ff @(posedge dst_clk) begin
    p1_q <= pulse_i;
    p2_q <= p1_q;
  end

  always_ff @(posedge dst_clk) begin
    if (!dst_rst_ni) seen_q <= 1'b0;
    else             seen_q <= seen_q | p2_q;
  end

  assign seen_o = seen_q;
endmodule
