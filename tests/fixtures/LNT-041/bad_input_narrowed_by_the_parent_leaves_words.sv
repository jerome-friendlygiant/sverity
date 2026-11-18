// sverity-args: --top top
// The parent zero-extends one bit into the 3-bit address port: only words 0 and 1 are ever written.
// expect: LNT-041 WARNING 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, raw, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa({2'b00, raw}), .wd, .ra, .rd);
endmodule
