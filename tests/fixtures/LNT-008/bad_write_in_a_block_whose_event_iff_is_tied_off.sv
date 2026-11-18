// sverity-args: --top top
// The block waits for a clock edge only while the enable is set and the only instance ties it to 0: it never runs and r is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, en, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always @(posedge clk iff en) r <= d;
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .en(1'b0), .d, .q);
endmodule
