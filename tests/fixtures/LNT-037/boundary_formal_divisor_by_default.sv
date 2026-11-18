// sverity-args: --top top
// Without the opt-in a divisor that is a subroutine's formal is not an obligation (the same module with the option on is unknown_formal_divisor_opt_in).
// expect-none: LNT-037
module top(input logic [15:0] numerator, input logic [7:0] divisor, output logic [15:0] quotient);
  function automatic logic [15:0] divide(input logic [15:0] n, input logic [7:0] d);
    return n / d;
  endfunction
  assign quotient = divide(numerator, divisor);
endmodule
