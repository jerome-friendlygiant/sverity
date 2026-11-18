// sverity-args: --top top
// A module the top never instantiates is elaborated by the compiler only for its own diagnostics; its drivers are not
// bound there. It is not hardware of this run, so no lint finding describes it (LNT-011 reports it unused).
// expect-none: LNT-024
// expect: LNT-011 INFO 9
module top(input logic a, output logic y);
  assign y = ~a;
endmodule
module spare(input logic a, output logic y);
  assign y = a;
endmodule
