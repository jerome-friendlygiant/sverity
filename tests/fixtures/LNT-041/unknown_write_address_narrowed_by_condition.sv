// sverity-args: --top top
// The write is guarded by a test of its own address, so which words are reached depends on that test: not decided here.
// expect-unresolved: LNT-041 5
module top(input logic clk, we, input logic [3:0] wa, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  always_ff @(posedge clk) if (we && wa < 4'd4) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
