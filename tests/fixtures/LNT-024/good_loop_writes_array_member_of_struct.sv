// sverity-args: --top top
// A loop writes every element of an array member of a struct, and the other member is written once.
// expect-none: LNT-024
typedef struct { logic [3:0] m; logic f; } s_t;
module top(input logic c, output s_t o);
  always_comb begin
    o.f = c;
    for (int i = 0; i < 4; i++) o.m[i] = c;
  end
endmodule
