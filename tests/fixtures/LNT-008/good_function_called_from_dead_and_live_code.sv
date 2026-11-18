// sverity-args: --top top
// One call of f is dead but another runs, so the variable it writes is driven.
// expect-none: LNT-008
module top(input logic [3:0] a, input logic s, output logic [3:0] y);
  logic [3:0] x;
  function automatic void f(input logic [3:0] v); x = v; endfunction
  always_comb begin
    if (1'b0) f(a);
    else if (s) f(~a);
    else f(a);
  end
  assign y = x;
endmodule
