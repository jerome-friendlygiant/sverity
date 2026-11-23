// sverity-args: --top top --sdc @DIR@/nonfirst_bit.sdc
// expect-count: CDC-038 WARNING 1
module top(input logic clka, clkb, rst_n, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= '0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin s1 <= '0; s2 <= '0; end else begin s1 <= a_q; s2 <= s1; end
  assign q = s2;
endmodule
