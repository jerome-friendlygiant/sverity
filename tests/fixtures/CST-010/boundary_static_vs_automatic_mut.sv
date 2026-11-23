// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary + mutation: static local triggers CST-010; changing to automatic local clears CST-010.
// expect: CST-010 WARNING
// revert: static logic tmp; => automatic logic tmp;
// revert-expect-none: CST-010 WARNING
module top(input logic clka, d, output logic q);
  function logic pass_v(input logic x);
    static logic tmp;
    tmp = x;
    return tmp;
  endfunction
  always_ff @(posedge clka) q <= pass_v(d);
endmodule
