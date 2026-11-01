// sverity-args: --top top
// revert: .nope(a) => .a(a)
// revert-expect-none: LNT-003
// A named connection to a port that does not exist is an illegal connection (slang error mapped to LNT-003).
// expect: LNT-003 ERROR
module sub(input logic a, output logic y);
  assign y = a;
endmodule
module top(input logic a, output logic y);
  sub u(.nope(a), .y(y));
endmodule
