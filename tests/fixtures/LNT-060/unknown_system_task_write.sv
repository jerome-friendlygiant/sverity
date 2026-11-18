// sverity-args: --top top
// expect-unresolved: LNT-060 6
module top(input logic clk, input logic [1:0] a, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) $readmemh("data.hex", mem);
  assign q = mem[a];
endmodule
