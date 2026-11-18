// sverity-args: --top top
// Lookalike of good_function_called_with_constant_counts: one call passes a 32-bit signal, so the count can be anything.
// revert: rotr(b, n) => rotr(b, 11)
// revert-expect-none: LNT-036 WARNING
// expect: LNT-036 WARNING 8
module top(input logic [31:0] a, b, input integer n, output logic [31:0] r);
  function automatic logic [31:0] rotr(input logic [31:0] x, input integer amt);
    return (x >> amt) | (x << (32 - amt));
  endfunction
  assign r = rotr(a, 6) ^ rotr(b, n);
endmodule
