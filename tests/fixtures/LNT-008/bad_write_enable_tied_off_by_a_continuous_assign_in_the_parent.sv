// sverity-args: --top top
// The parent drives the enable net with a constant 0 through an `assign`, so the child never writes r.
// expect: LNT-008 WARNING 5
module child(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (en) r <= d;
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  wire off;
  assign off = 1'b0;
  child u(.clk, .en(off), .d, .q);
endmodule
