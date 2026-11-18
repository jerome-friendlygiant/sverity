// sverity-args: --top top
// The child's address port is a free input in the child, but this instance ties it to 3'd5: only word 5 is ever written.
// expect: LNT-041 WARNING 5
module child(input logic clk, we, input logic [2:0] wa, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  logic [7:0] mem [8];
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  assign rd = mem[ra];
endmodule
module top(input logic clk, we, input logic [7:0] wd, input logic [2:0] ra, output logic [7:0] rd);
  child u(.clk, .we, .wa(3'd5), .wd, .ra, .rd);
endmodule
