// Event counter read in another clock domain.
module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       tick_i,
  output logic [3:0] cnt_o
);
  logic [3:0] cnt_q;
  logic [3:0] s1_q;
  logic [3:0] s2_q;
  logic [3:0] snap_q;

  always_ff @(posedge clk_a) if (tick_i) cnt_q <= cnt_q + 4'd1;

  always_ff @(posedge clk_b) begin
    s1_q <= cnt_q;
    s2_q <= s1_q;
  end

  always_ff @(posedge clk_b) snap_q <= s2_q;

  assign cnt_o = snap_q;
endmodule
