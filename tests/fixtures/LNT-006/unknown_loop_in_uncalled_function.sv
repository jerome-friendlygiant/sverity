// sverity-args: --top top
// A function nobody calls here overruns its array. The code may be reused, so the overrun stays a finding, Unresolved.
// expect: LNT-006 WARNING 7
module top(input logic [3:0] a, output logic y);
  function automatic logic f(input logic [3:0] v);
    logic r = 0;
    for (int i = 0; i < 5; i++) r ^= v[i];
    return r;
  endfunction
  assign y = ^a;
endmodule
