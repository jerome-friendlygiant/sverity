// sverity-args: --top top
// The function is static, so s keeps its value from call to call: the combinational block that calls it holds state.
// expect: LNT-013 WARNING 7
module top(input logic [3:0] a, output logic [3:0] y);
  function logic [3:0] acc(input logic [3:0] v);
    logic [3:0] s;
    s = s + v;
    return s;
  endfunction
  always_comb y = acc(a);
endmodule
