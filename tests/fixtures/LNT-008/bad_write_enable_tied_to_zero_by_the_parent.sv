// sverity-args: --top top
// The only instance ties the write enable to 1'b0: the write builds no hardware, so the memory is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we(1'b0), .wa, .wd, .ra, .rd);
endmodule
