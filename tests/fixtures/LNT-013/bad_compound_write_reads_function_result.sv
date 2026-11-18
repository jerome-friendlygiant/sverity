// sverity-args: --top top
// f += v reads the result before anything assigned it: it starts from the earlier call's value (static function).
// expect: LNT-013 WARNING 5
module top(input logic [3:0] a, output logic [3:0] y);
  function logic [3:0] f(input logic [3:0] v);
    f += v;
  endfunction
  always_comb y = f(a);
endmodule
