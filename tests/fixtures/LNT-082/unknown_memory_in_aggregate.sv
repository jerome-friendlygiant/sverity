// The words of a memory inside a struct are not attributed.
// sverity-args: --top top
// expect: LNT-082 WARNING 8
module top(input logic clk, input logic [1:0] wa, ra, input logic [7:0] d, output logic [7:0] q);
  typedef struct { logic [7:0] m [4]; logic v; } store_t;
  store_t s;
  always_ff @(posedge clk) s.m[wa] <= d;
  always_ff @(posedge clk) q <= s.m[ra];
endmodule
