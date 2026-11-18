// sverity-args: --top top
// A static function variable read first, in a function an always_latch calls: the latch is meant to hold y, not a hidden variable.
// expect: LNT-013 WARNING 7
module top(input logic [3:0] a, input logic c, output logic [3:0] y);
  function logic [3:0] f(input logic [3:0] v);
    logic [3:0] s;
    s = s ^ v;
    return s;
  endfunction
  always_latch if (c) y = f(a);
endmodule
