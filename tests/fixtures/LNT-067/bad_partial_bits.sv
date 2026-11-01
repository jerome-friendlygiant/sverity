// sverity-args: --top top
// only bit 0 of the 8-bit register is reset; bits 7:1 have no reset value
// expect: LNT-067 WARNING
module top(input logic clk, rst_n, input logic [7:0] d, output logic [7:0] q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q[0] <= 1'b0; else q <= d;
endmodule
