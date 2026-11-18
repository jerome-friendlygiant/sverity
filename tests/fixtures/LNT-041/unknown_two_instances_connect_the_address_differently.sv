// sverity-args: --top top
// One instance forwards a free address, the other a narrow one: the shared body cannot say which words the second writes.
// expect-unresolved: LNT-041 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, raw, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd, rd2);
  child u0(.clk, .we, .wa, .wd, .ra, .rd);
  child u1(.clk, .we, .wa({2'b00, raw}), .wd, .ra, .rd(rd2));
endmodule
