// sverity-args: --top top
// Each iteration writes both members of element i of an array of structs: every bit is named.
// expect-none: LNT-024
typedef struct packed { logic [3:0] d; logic q; } e_t;
module top(input logic c, output e_t o [4]);
  always_comb begin
    for (int i = 0; i < 4; i++) begin
      o[i].d = {4{c}};
      o[i].q = c;
    end
  end
endmodule
