// sverity-args: --top top
// expect: LNT-010 WARNING
module child(input logic a, b, output logic y);
  assign y = a & b;
endmodule
module top(input logic a, output logic y);
  child u(.a(a), .y(y));
endmodule
// revert: .a(a), .y(y) => .a(a), .b(a), .y(y)
// revert-expect-none: LNT-010
