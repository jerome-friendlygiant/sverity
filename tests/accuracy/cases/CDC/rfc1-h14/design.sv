// Gray-coded write pointer observed in another clock domain.
module top (
  input  logic       clk_a,
  input  logic       clk_b,
  input  logic       inc_i,
  output logic [3:0] gray_o
);
  logic [3:0] bin_q;
  logic [3:0] gray_q;
  logic [3:0] s1_q;
  logic [3:0] s2_q;

  always_ff @(posedge clk_a) if (inc_i) bin_q <= bin_q + 4'd1;
  always_ff @(posedge clk_a) gray_q <= bin_q ^ (bin_q >> 1);

  always_ff @(posedge clk_b) begin
    s1_q[3:1] <= gray_q[3:1];
    s2_q[3:1] <= s1_q[3:1];
    s2_q[0]   <= gray_q[0];
  end

  assign gray_o = s2_q;
endmodule
