// Reset generator that releases a producer and a consumer domain from one lock indication.
module top (
  input  logic clk_a,
  input  logic clk_b,
  input  logic por_ni,
  input  logic pll_locked_i,
  output logic q_b_o
);
  logic rst_a_n, rst_b_n, b1_q, q_a_q;

  always_ff @(posedge clk_a or negedge por_ni)
    if (!por_ni) rst_a_n <= 1'b0;
    else         rst_a_n <= pll_locked_i;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) q_a_q <= 1'b0;
    else          q_a_q <= 1'b1;

  always_ff @(posedge clk_b or negedge por_ni)
    if (!por_ni) begin
      b1_q    <= 1'b0;
      rst_b_n <= 1'b0;
    end else begin
      b1_q    <= pll_locked_i;
      rst_b_n <= b1_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) q_b_o <= 1'b0;
    else          q_b_o <= q_a_q;

endmodule
