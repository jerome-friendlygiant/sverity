// sverity-args: --top top
// The tie kills the first arm, but the live arm writes the whole vector: every bit has a driver.
// expect-none: LNT-008
module leaf(input logic clk, en, input logic [7:0] a, b, output logic [7:0] q);
  logic [7:0] r;
  always_ff @(posedge clk) if (!en) r <= a; else r <= b;
  assign q = r;
endmodule
module top(input logic clk, input logic [7:0] a, b, output logic [7:0] q);
  leaf u(.clk, .en(1'b1), .a, .b, .q);
endmodule
