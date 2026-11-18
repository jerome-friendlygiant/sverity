// sverity-args: --top top
// A 2-bit slice of a free input reaches words 0 to 3 of the 8 (zero-extended into the 3-bit address): words 4 to 7 are never written.
// expect: LNT-041 WARNING 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [31:0] addr, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa(addr[3:2]), .wd, .ra, .rd);
endmodule
