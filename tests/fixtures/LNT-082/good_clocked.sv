// sverity-args: --top top
// expect-none: LNT-082
module top(input logic clk, input logic [1:0] a, output logic [3:0] q); logic [3:0] mem [4]; always_ff @(posedge clk) q<=mem[a]; endmodule
