// sverity-args: --top top --option LNT-037.dynamic_divisor=true
// A subroutine's formal is the caller's to constrain: reported only on request, when the callers pass nothing that excludes zero.
// expect: LNT-037 WARNING 6
module top(input logic [15:0] numerator, input logic [7:0] divisor, output logic [15:0] quotient);
  function automatic logic [15:0] divide(input logic [15:0] n, input logic [7:0] d);
    return n / d;
  endfunction
  assign quotient = divide(numerator, divisor);
endmodule
