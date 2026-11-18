// A memory inside a struct that is only written has no read to collide with.
// sverity-args: --top top
// expect-none: LNT-082
module top(input logic clk, input logic [1:0] wa, input logic [7:0] d);
  typedef struct { logic [7:0] m [4]; logic v; } store_t;
  store_t s;
  always_ff @(posedge clk) s.m[wa] <= d;
endmodule
