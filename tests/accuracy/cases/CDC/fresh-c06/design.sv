module top(input logic clk_a, clk_b, input logic rst_a_n, rst_b_n, output logic [2:0] wseen);
  logic [2:0] wgray, bin_q;
  always_ff @(posedge clk_a) if (!rst_a_n) begin bin_q <= '0; wgray <= '0; end else begin bin_q <= bin_q + 1'b1; wgray <= (bin_q + 1'b1) ^ ((bin_q + 1'b1) >> 1); end
  always_ff @(posedge clk_b) if (!rst_b_n) wseen <= '0; else wseen <= wgray;
endmodule
