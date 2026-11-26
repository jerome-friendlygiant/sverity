// Pointer bridge that lets a consumer in one clock domain judge FIFO occupancy from a producer pointer.
module top (
  input  logic clk_a,
  input  logic rst_a_n,
  input  logic clk_b,
  input  logic rst_b_n,
  input  logic push_i,
  input  logic pop_i,
  output logic empty_o
);
  logic [3:0] wbin_q, wgray_q, wsync1_q, wsync_q, rbin_q;

  always_ff @(posedge clk_a or negedge rst_a_n)
    if (!rst_a_n) begin
      wbin_q  <= '0;
      wgray_q <= '0;
    end else if (push_i) begin
      wbin_q  <= wbin_q + 4'd1;
      wgray_q <= (wbin_q + 4'd1) ^ ((wbin_q + 4'd1) >> 1);
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) begin
      wsync1_q <= '0;
      wsync_q  <= '0;
    end else begin
      wsync1_q <= wgray_q;
      wsync_q  <= wsync1_q;
    end

  always_ff @(posedge clk_b or negedge rst_b_n)
    if (!rst_b_n) rbin_q <= '0;
    else if (pop_i && !empty_o) rbin_q <= rbin_q + 4'd1;

  assign empty_o = (wsync_q == (rbin_q ^ (rbin_q >> 1)));

endmodule
