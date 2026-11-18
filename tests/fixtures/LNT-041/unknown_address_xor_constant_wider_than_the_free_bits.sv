// sverity-args: --top top
// The free operand has 2 bits but the XOR is 3 bits wide: the constant sets bit 2, so the address reaches words 4 to 7 only. It is not free, so no word is claimed written.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [2:0] p, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa(p[1:0] ^ 3'd4), .wd, .ra, .rd);
endmodule
