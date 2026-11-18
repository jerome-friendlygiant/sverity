// sverity-args: --top top
// The guard tests the input the address is copied from, so it narrows the address as well.
// expect-unresolved: LNT-041 5
module top(input logic clk, we, input logic [3:0] raw, ra, input logic [7:0] wd, output logic [7:0] rd);
  logic [7:0] mem [16];
  logic [3:0] wa;
  assign wa = raw;
  always_ff @(posedge clk) if (we && raw < 4'd4) mem[wa] <= wd;
  always_ff @(posedge clk) rd <= mem[ra];
endmodule
