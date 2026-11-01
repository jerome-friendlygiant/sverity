// sverity-args: --top top
// expect-none: STY-004
module child(input logic a, output logic y);
  assign y = a;
endmodule
module top(input logic a, output logic y);
  child u0 (.a(a), .y(y));
endmodule
