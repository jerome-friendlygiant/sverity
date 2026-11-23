// sverity-args: --top top --sdc @FIXTURES@/CDC-003/async.sdc
// Transfers through subroutine-local static state must be disclosed via CST-010 and mark graph-precision INCOMPLETE.
// expect: CST-010 WARNING
// expect-unresolved: CST-010
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clka, clkb, d, output logic q);
  function logic stash(input logic v, input logic wr);
    static logic hidden;
    if (wr) hidden = v;
    return hidden;
  endfunction
  logic a_sink;
  always_ff @(posedge clka) a_sink <= stash(d, 1'b1);
  always_ff @(posedge clkb) q <= stash(1'b0, 1'b0);
endmodule
