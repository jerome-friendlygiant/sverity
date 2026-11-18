// sverity-args: --top top
// The write needs the two-bit mode to equal 2'd3; the only instance ties it to 2'd1, so r is never driven.
// expect: LNT-008 WARNING 5
module child(input logic clk, input logic [1:0] mode, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (mode == 2'd3) r <= d;
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] d, output logic [7:0] q);
  child u(.clk, .mode(2'd1), .d, .q);
endmodule
