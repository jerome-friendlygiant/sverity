// sverity-args: --top top
// Bit 0 is reset, bits 7:1 are loaded: one finding for the register, naming the bits left out.
// expect-count: LNT-067 INFO 1
// expect: LNT-067 INFO 6
module top(input logic clk, rst_n, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q[0] <= 1'b0; else q <= d;
endmodule
