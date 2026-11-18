// sverity-args: --top top
// The two run-time indices cannot meet: one has its top bit forced to 0 (words 0 and 1), the other to 1 (words 2 and 3).
// expect-none: LNT-049
module top(input logic clk, we, input logic x, y, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] m [4];
  always_ff @(posedge clk) if (we) m[{1'b0, x}] <= d;
  always_ff @(posedge clk) if (we) m[{1'b1, y}] <= ~d;
  assign q = m[0];
endmodule
