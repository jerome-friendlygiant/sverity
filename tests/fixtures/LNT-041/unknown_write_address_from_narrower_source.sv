// sverity-args: --top top
// The address is a 4-bit wire, but it is fed by a 2-bit input, so only words 0..3 are ever written. The type range of the index is not
// the set of values it takes: whether the other words are written is not decided.
// expect-unresolved: LNT-041 6
module top(input logic clk, we, input logic [1:0] raw, input logic [3:0] ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  logic [3:0] wa;
  assign wa = {2'b00, raw};
  always_ff @(posedge clk) if (we) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
