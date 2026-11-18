// sverity-args: --top top
// revert: USE = 1 => USE = 0
// revert-expect-none: LNT-006 ERROR
// The generate branch that calls g exists, so g runs and its loop overruns the four-bit argument.
// expect: LNT-006 ERROR 9
module top #(parameter bit USE = 1)(input logic [3:0] a, output logic y);
  function automatic logic g(input logic [3:0] v);
    logic r = 0;
    for (int i = 0; i < 5; i++) r ^= v[i];
    return r;
  endfunction
  if (USE) begin : gg assign y = g(a); end else begin : gn assign y = ^a; end
endmodule
