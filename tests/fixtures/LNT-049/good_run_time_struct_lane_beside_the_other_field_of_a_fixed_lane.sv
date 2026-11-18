// sverity-args: --top top
// Lanes of a packed struct pair: one process writes the high field of lane s (bits 2-3 or 6-7), the other the low field of lane 1 (bits 4-5).
// expect-none: LNT-049 WARNING
module top(input logic clk, s, input logic [1:0] a, b, output logic [1:0] y);
  typedef struct packed { logic [1:0] hi; logic [1:0] lo; } lane_t;
  lane_t [1:0] m [2];
  always_ff @(posedge clk) m[0][s].hi <= a;
  always_ff @(posedge clk) m[0][1].lo <= b;
  assign y = m[0][0].lo;
endmodule
