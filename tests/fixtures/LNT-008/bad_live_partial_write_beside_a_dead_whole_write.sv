// sverity-args: --top top
// The only instance ties en to 1: the whole-vector write in the !en arm never runs, the live write drives bit 0 only, so bits 7:1 have no driver.
// expect: LNT-008 WARNING 5
module leaf(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (!en) r <= d; else r[0] <= d[0];
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  leaf u(.clk, .en(1'b1), .d, .q);
endmodule
