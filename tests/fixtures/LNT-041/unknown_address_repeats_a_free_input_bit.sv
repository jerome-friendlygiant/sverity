// sverity-args: --top top
// The concatenation uses bit 0 of the input twice: its bits are not independent, so it does not take every value of its width.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [2:0] a, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa({a[0], a[0], a[1]}), .wd, .ra, .rd);
endmodule
