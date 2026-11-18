// sverity-args: --top top
// The index s.k, a two-bit member of a packed struct, takes 0 to 3, which reaches the bit the other process writes.
// expect: LNT-009 ERROR 7
typedef struct packed {logic [1:0] k; logic f;} s_t;
module top(input logic clk, input s_t s, input logic a, b, output logic [3:0] y);
  always @(posedge clk) y[s.k] <= a;
  always @(posedge clk) y[3] <= b;
endmodule
