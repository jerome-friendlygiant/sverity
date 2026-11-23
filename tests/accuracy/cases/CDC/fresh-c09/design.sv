module top(input logic clk_a, clk_b, input logic rst_a_n, rst_b_n, output logic [2:0] gray0_b, gray1_b);
  logic [2:0] bin0, bin1, gray0_a, gray1_a, s0_1, s0_2, s1_1, s1_2;
  always_ff @(posedge clk_a) if (!rst_a_n) begin bin0 <= '0; bin1 <= '0; gray0_a <= '0; gray1_a <= '0; end else begin bin0 <= bin0 + 1'b1; bin1 <= bin1 + 1'b1; gray0_a <= (bin0 + 1'b1) ^ ((bin0 + 1'b1) >> 1); gray1_a <= (bin1 + 1'b1) ^ ((bin1 + 1'b1) >> 1); end
  always_ff @(posedge clk_b) if (!rst_b_n) begin s0_1 <= '0; s0_2 <= '0; s1_1 <= '0; s1_2 <= '0; end else begin s0_1 <= gray0_a; s0_2 <= s0_1; s1_1 <= gray1_a; s1_2 <= s1_1; end
  always_comb begin gray0_b = s0_2; gray1_b = s1_2; end
endmodule
