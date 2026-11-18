// sverity-args: --top top
// u0 ties the enable to 0, u1 leaves it free: the shared body writes in one instance only, and the memory of u0 is never driven.
// expect-unresolved: LNT-041 6
// expect-none: LNT-008
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] r0, r1);
  child u0(.clk, .we(1'b0), .wa, .wd, .ra, .rd(r0));
  child u1(.clk, .we, .wa, .wd, .ra, .rd(r1));
endmodule
