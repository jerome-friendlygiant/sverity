// sverity-args: --top top
// `go()` returns 0 when first evaluated, but its static counter makes it 1 on later activations: the write is hardware. A condition
// that calls a subroutine is never taken as constant, so the write is not dead code and its coverage is not determinable.
// expect: LNT-008 WARNING 6
module top(input logic c, output logic y);
  logic s;
  function automatic int go(); static int k = 0; k++; return k > 1; endfunction
  always @(c) if (go()) s = c;
  assign y = s;
endmodule
